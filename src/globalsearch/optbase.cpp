/**********************************************************************
  OptBase - Base class for global search extensions

  Copyright (C) 2010-2011 by David C. Lonie

  This source code is released under the New BSD License, (the "License").

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 ***********************************************************************/

#include <globalsearch/optbase.h>

#include <globalsearch/bt.h>
#include <globalsearch/eleminfo.h>
#include <globalsearch/formats/poscarformat.h>
#include <globalsearch/http/aflowml.h>
#include <globalsearch/macros.h>
#include <globalsearch/optimizer.h>
#include <globalsearch/queueinterface.h>
#include <globalsearch/queuemanager.h>
#ifdef ENABLE_SSH
#include <globalsearch/sshconnection.h>
#include <globalsearch/sshmanager.h>
#ifdef USE_CLI_SSH
#include <globalsearch/sshmanager_cli.h>
#else // USE_CLI_SSH
#include <globalsearch/sshmanager_libssh.h>
#endif // USE_CLI_SSH
#endif // ENABLE_SSH
#include <globalsearch/structure.h>
#include <globalsearch/ui/abstractdialog.h>
#include <globalsearch/utilities/makeunique.h>
#include <globalsearch/utilities/passwordprompt.h>
#include <globalsearch/utilities/utilityfunctions.h>

#ifdef ENABLE_MOLECULAR
#include <globalsearch/molecular/conformergenerator.h>
#endif // ENABLE_MOLECULAR

#include <QDebug>
#include <QFile>
#include <QThread>

#include <QApplication>
#include <QClipboard>
#include <QInputDialog>
#include <QMessageBox>
#include <QtConcurrent>

#include <cfloat>
#include <cmath>
#include <fstream>
#include <iostream>
#include <mutex>
#include <chrono>
#include <thread>

//#define OPTBASE_DEBUG

namespace GlobalSearch {

OptBase::OptBase(AbstractDialog* parent)
  : QObject(parent),
#ifdef ENABLE_MOLECULAR
    m_initialMolFile(""), m_conformerOutDir(""), m_numConformersToGenerate(0),
    m_rmsdThreshold(0.1), m_maxOptIters(1000), m_mmffOptConfs(false),
    m_pruneConfsAfterOpt(true),
#endif // ENABLE_MOLECULAR
    cutoff(-1), testingMode(false), test_nRunsStart(1), test_nRunsEnd(100),
    test_nStructs(600), stateFileMutex(new QMutex), readOnly(false),
    m_idString("Generic"),
#ifdef ENABLE_SSH
    m_ssh(nullptr),
#endif // ENABLE_SSH
    m_dialog(parent), m_tracker(new Tracker(this)), m_queueThread(new QThread),
    m_queue(new QueueManager(m_queueThread, this)), m_numOptSteps(0),
    m_schemaVersion(3), m_usingGUI(true),
#ifdef ENABLE_MOLECULAR
    m_molecularMode(false),
#endif // ENABLE_MOLECULAR
    m_logErrorDirs(false), m_calculateHardness(false),
    m_hardnessFitnessWeight(0.0),
    m_networkAccessManager(std::make_shared<QNetworkAccessManager>()),
    m_aflowML(make_unique<AflowML>(m_networkAccessManager, this))
{
  // Connections
  connect(this, SIGNAL(sessionStarted()), m_queueThread, SLOT(start()),
          Qt::DirectConnection);
  connect(this, SIGNAL(startingSession()), m_queueThread, SLOT(start()),
          Qt::DirectConnection);
  connect(this, SIGNAL(startingSession()), this, SLOT(setIsStartingTrue()),
          Qt::DirectConnection);
  connect(this, SIGNAL(sessionStarted()), this, SLOT(setIsStartingFalse()),
          Qt::DirectConnection);
  connect(this, SIGNAL(readOnlySessionStarted()), this,
          SLOT(setIsStartingFalse()), Qt::DirectConnection);
  connect(this, SIGNAL(needBoolean(const QString&, bool*)), this,
          SLOT(promptForBoolean(const QString&, bool*)),
          Qt::BlockingQueuedConnection); // Wait until slot returns
  connect(this, SIGNAL(needPassword(const QString&, QString*, bool*)), this,
          SLOT(promptForPassword(const QString&, QString*, bool*)),
          Qt::BlockingQueuedConnection); // Wait until slot returns
  connect(this, SIGNAL(sig_setClipboard(const QString&)), this,
          SLOT(setClipboard_(const QString&)), Qt::QueuedConnection);
  connect(m_tracker, &Tracker::newStructureAdded,
          [this]() { QtConcurrent::run([this]() { this->save("", false); }); });
  connect(m_queue, &QueueManager::structureUpdated,
          [this]() { QtConcurrent::run([this]() { this->save("", false); }); });
  connect(m_queue, &QueueManager::structureFinished, this,
          &OptBase::calculateHardness);
  connect(m_aflowML.get(), &AflowML::received, this,
          &OptBase::finishHardnessCalculation);
}

OptBase::~OptBase()
{
  delete m_queue;
  m_queue = 0;

  if (m_queueThread && m_queueThread->isRunning()) {
    m_queueThread->wait();
  }
  delete m_queueThread;
  m_queueThread = 0;

  delete m_tracker;
  m_tracker = 0;
}

void OptBase::reset()
{
  m_tracker->lockForWrite();
  m_tracker->deleteAllStructures();
  m_tracker->reset();
  m_tracker->unlock();
  m_queue->reset();
}

#ifdef ENABLE_SSH
bool OptBase::createSSHConnections()
{
#ifdef USE_CLI_SSH
  return this->createSSHConnections_cli();
#else  // USE_CLI_SSH
  return this->createSSHConnections_libssh();
#endif // USE_CLI_SSH
}
#endif // ENABLE_SSH

static inline double calculateProb(double currentEnthalpy,
                                   double currentHardness,
                                   double lowestEnthalpy,
                                   double highestEnthalpy,
                                   double lowestHardness,
                                   double highestHardness,
                                   double hardnessWeight)
{
  double enthalpySpread = highestEnthalpy - lowestEnthalpy;
  double hardnessSpread = highestHardness - lowestHardness;
  return 1.0 - hardnessWeight * (highestHardness - currentHardness) /
         hardnessSpread -
         (1.0 - hardnessWeight) * (currentEnthalpy - lowestEnthalpy) /
         enthalpySpread;
}

// Uncomment this to print out probability debug information
//#define OPTBASE_PROBS_DEBUG

QList<QPair<Structure*, double>>
OptBase::getProbabilityList(const QList<Structure*>& structures,
                            size_t popSize,
                            double hardnessWeight)
{
  QList<QPair<Structure*, double>> probs;
  if (structures.isEmpty() || popSize == 0)
    return probs;

  if (structures.size() == 1) {
    probs.append(QPair<Structure*, double>(structures[0], 1.0));
    return probs;
  }

  // Since enthalpy can be negative, we will make -DBL_MAX the starting value
  // for highestEnthalpy. But since hardness can't be negative, we will use
  // DBL_MIN as the starting value for highestHardness.
  double lowestEnthalpy  =  DBL_MAX;
  double highestEnthalpy = -DBL_MAX;
  double lowestHardness  =  DBL_MAX;
  double highestHardness =  DBL_MIN;

  // Find the lowest and highest of each
  for (const auto& s: structures) {
    QReadLocker lock(&s->lock());
    const auto& enthalpy = s->getEnthalpyPerFU();
    if (enthalpy < lowestEnthalpy)
      lowestEnthalpy = enthalpy;
    if (enthalpy > highestEnthalpy)
      highestEnthalpy = enthalpy;

    const auto& hardness = s->vickersHardness();
    if (hardness < lowestHardness)
      lowestHardness = hardness;
    if (hardness > highestHardness)
      highestHardness = hardness;
  }

#ifdef OPTBASE_PROBS_DEBUG
  std::cout << "lowestEnthalpy is: "  << lowestEnthalpy  << "\n";
  std::cout << "highestEnthalpy is: " << highestEnthalpy << "\n";
  std::cout << "lowestHardness is: "  << lowestHardness  << "\n";
  std::cout << "highestHardness is: " << highestHardness << "\n";
  std::cout << "Unnormalized, unsorted, and untrimmed probs list is:\n";
  std::cout << "Structure : enthalpy : hardness : probs\n";
#endif

  // Now calculate the probability of each structure
  for (const auto& s: structures) {
    QReadLocker lock(&s->lock());
    double prob = calculateProb(s->getEnthalpyPerFU(),
                                s->vickersHardness(),
                                lowestEnthalpy,
                                highestEnthalpy,
                                lowestHardness,
                                highestHardness,
                                hardnessWeight);
    probs.append(QPair<Structure*, double>(s, prob));

#ifdef OPTBASE_PROBS_DEBUG
    std::cout << s->getGeneration() << "x"
              << s->getIDNumber() << " : "
              << s->getEnthalpyPerFU() << " : "
              << s->vickersHardness() << " : " << prob << "\n";
#endif
  }

  // If they are all nan, that means all the probs are equal. Just
  // return an equal list
  bool allNan = true;
  for (const auto& prob: probs) {
    if (!std::isnan(prob.second)) {
      allNan = false;
      break;
    }
  }

  if (allNan) {
    double dref = 1.0 / probs.size();
    double sum = 0.0;

    for (auto& prob: probs) {
      prob.second = sum;
      sum += dref;
    }
    return probs;
  }

  // Sort by probability
  std::sort(probs.begin(), probs.end(),
            [](const QPair<Structure*, double>& a,
               const QPair<Structure*, double>& b)
            {
              return a.second < b.second;
            });

  // Remove the lowest probability structures until we have the pop size
  while (probs.size() > popSize)
    probs.pop_front();


#ifdef OPTBASE_PROBS_DEBUG
  std::cout << "Unnormalized (but sorted and trimmed) probs list is:\n";
  std::cout << "Structure : enthalpy : hardness : probs\n";
  for (const auto& elem: probs) {
    QReadLocker lock(&elem.first->lock());
    std::cout << elem.first->getGeneration() << "x"
              << elem.first->getIDNumber() << " : "
              << elem.first->getEnthalpyPerFU() << " : "
              << elem.first->vickersHardness() << " : " << elem.second << "\n";
  }
#endif

  // Sum the resulting probs
  double sum = 0.0;
  for (const auto& elem: probs)
    sum += elem.second;

  // Normalize the list so that the sum is 1
  for (auto& elem: probs)
    elem.second /= sum;

#ifdef OPTBASE_PROBS_DEBUG
  std::cout << "Normalized, sorted, and trimmed probs list is:\n";
  std::cout << "Structure : enthalpy : hardness : probs\n";
  for (const auto& elem: probs) {
    QReadLocker lock(&elem.first->lock());
    std::cout << elem.first->getGeneration() << "x"
              << elem.first->getIDNumber() << " : "
              << elem.first->getEnthalpyPerFU() << " : "
              << elem.first->vickersHardness() << " : " << elem.second << "\n";
  }
#endif

  // Now replace each entry with a cumulative total
  sum = 0.0;
  for (auto& elem: probs) {
    sum += elem.second;
    elem.second = sum;
  }

#ifdef OPTBASE_PROBS_DEBUG
  std::cout << "Cumulative (final) probs list is:\n";
  std::cout << "Structure : enthalpy : hardness : probs\n";
  for (const auto& elem: probs) {
    QReadLocker lock(&elem.first->lock());
    std::cout << elem.first->getGeneration() << "x"
              << elem.first->getIDNumber() << " : "
              << elem.first->getEnthalpyPerFU() << " : "
              << elem.first->vickersHardness() << " : " << elem.second << "\n";
  }
#endif

  return probs;
}

// Start up a resubmission thread that will attempt resubmissions every
// 10 minutes
void OptBase::startHardnessResubmissionThread()
{
  if (!m_calculateHardness)
    return;

  // Run in a separate thread
  // This is our first usage of std::thread in the program. If we run into
  // linking or packaging problems, we can go back to QtConcurrent
  std::thread(&OptBase::_startHardnessResubmissionThread, this).detach();
}

void OptBase::_startHardnessResubmissionThread()
{
  if (!m_calculateHardness)
    return;

  // Make sure we only ever have one of these going at a time
  static std::mutex resubmissionMutex;
  std::unique_lock<std::mutex> lock(resubmissionMutex, std::defer_lock);
  if (!lock.try_lock())
    return;

  while (m_calculateHardness) {
    // Wait 10 minutes before the resubmission
    std::this_thread::sleep_for(std::chrono::minutes(10));
    resubmitUnfinishedHardnessCalcs();
  }

  lock.unlock();

  // Run this function again if m_calculateHardness became true in
  // between the "while" check and the unlocking of the mutex
  if (m_calculateHardness)
    _startHardnessResubmissionThread();
}

void OptBase::calculateHardness(Structure* s)
{
  // If we are not to calculate hardness, do nothing
  if (!m_calculateHardness)
    return;

  // Convert the structure to a POSCAR file
  std::stringstream ss;
  PoscarFormat::write(*s, ss);

  QString id = QString::number(s->getGeneration()) + "x" +
               QString::number(s->getIDNumber());
  qDebug() << "Submitting structure" << id << "for Aflow ML calculation...";
  size_t ind = m_aflowML->submitPoscar(ss.str().c_str());
  m_pendingHardnessCalculations[ind] = s;
}

void OptBase::resubmitUnfinishedHardnessCalcs()
{
  if (!m_calculateHardness)
    return;

  QReadLocker trackerLocker(m_tracker->rwLock());
  QList<Structure*> structures = m_queue->getAllOptimizedStructures();
  structures.append(m_queue->getAllDuplicateStructures());
  structures.append(m_queue->getAllSupercellStructures());
  for (auto& s : structures) {
    if (s->vickersHardness() < 0.0)
      calculateHardness(s);
  }
}

void OptBase::_finishHardnessCalculation(size_t ind)
{
  // Let's use a mutex so this function can't be run in multiple threads at
  // once
  static std::mutex mutex;
  std::unique_lock<std::mutex> lock(mutex);

  // First, make sure we have this index
  auto it = m_pendingHardnessCalculations.find(ind);

  if (it == m_pendingHardnessCalculations.end()) {
    qDebug() << "Error in" << __FUNCTION__
             << ": Received hardness data for index" << ind << ", but could"
             << "not find the structure for this index!";
    return;
  }

  Structure* s = it->second;
  m_pendingHardnessCalculations.erase(ind);

  QString id = QString::number(s->getGeneration()) + "x" +
               QString::number(s->getIDNumber());
  qDebug() << "Received Aflow ML data for structure" << id;

  // Make sure AflowML actually has the data
  if (!m_aflowML->containsData(ind)) {
    qDebug() << "Error in" << __FUNCTION__
             << ": Received hardness data for index" << ind << ", but could"
             << "not find the AflowMLData for this index!";
    return;
  }

  AflowMLData data = m_aflowML->data(ind);
  m_aflowML->eraseData(ind);

  // Also make sure the structure is still in the tracker
  // Just skip over it if it isn't
  QReadLocker trackerLocker(m_tracker->rwLock());
  if (!m_tracker->contains(s))
    return;

  double bulkModulus = atof(data["ml_ael_bulk_modulus_vrh"].c_str());
  double shearModulus = atof(data["ml_ael_shear_modulus_vrh"].c_str());

  //double k = shearModulus / bulkModulus;

  // The Chen model: 2.0 * (k^2 * shear)^0.585 - 3.0
  //double hardness = 2.0 * pow((pow(k, 2.0) * shearModulus), 0.585) - 3.0;

  // The Teter model: 0.151 * shear
  double hardness = 0.151 * shearModulus;

  QWriteLocker structureLocker(&s->lock());
  s->setBulkModulus(bulkModulus);
  s->setShearModulus(shearModulus);
  s->setVickersHardness(hardness);
}

void OptBase::finishHardnessCalculation(size_t ind)
{
  // Run in a separate thread
  QtConcurrent::run(this, &OptBase::_finishHardnessCalculation, ind);
}

bool OptBase::save(QString stateFilename, bool notify)
{
  if (isStarting || readOnly)
    return false;

  QReadLocker trackerLocker(m_tracker->rwLock());
  QMutexLocker locker(stateFileMutex);
  QString filename;
  if (stateFilename.isEmpty()) {
    filename = filePath + "/" + m_idString.toLower() + ".state";
  } else {
    filename = stateFilename;
  }
  QString oldfilename = filename + ".old";

  if (notify && m_dialog) {
    m_dialog->startProgressUpdate(tr("Saving: Writing %1...").arg(filename), 0,
                                  0);
  }

  SETTINGS(filename);

  // Copy .state -> .state.old
  if (QFile::exists(filename)) {
    // Only copy over if the current state is valid
    const bool saveSuccessful =
      settings->value(m_idString.toLower().append("/saveSuccessful"), false)
        .toBool();
    if (saveSuccessful) {
      if (QFile::exists(oldfilename)) {
        QFile::remove(oldfilename);
      }
      QFile::copy(filename, oldfilename);
    }
  }

  const int version = m_schemaVersion;
  settings->beginGroup(m_idString.toLower());
  settings->setValue("version", version);
  settings->setValue("saveSuccessful", false);
  settings->endGroup();

  // Write/update .state
  if (m_dialog)
    m_dialog->writeSettings(filename);

  // Loop over structures and save them
  QList<Structure*>* structures = m_tracker->list();

  QString structureStateFileName, oldStructureStateFileName;

  Structure* structure;
  for (int i = 0; i < structures->size(); i++) {
    structure = structures->at(i);
    QReadLocker structureLocker(&structure->lock());
    // Set index here -- this is the only time these are written, so
    // this is "ok" under a read lock because of the savePending logic
    structure->setIndex(i);
    structureStateFileName = structure->fileName() + "/structure.state";
    oldStructureStateFileName = structureStateFileName + ".old";

    // We are going to write to structure.state.old if one already exists
    // and is a valid state file. This is done in response to
    // structure.state files being mysteriously empty on rare occasions...
    if (QFile::exists(structureStateFileName)) {

      // Attempt to open state file. We will make sure it is valid
      QFile file(structureStateFileName);
      if (!file.open(QIODevice::ReadOnly)) {
        error("OptBase::save(): Error opening file " + structureStateFileName +
              " for reading...");
        return false;
      }

      // If the state file is empty or if saveSuccessful is false,
      // stateFileIsValid will be false. This will hopefully not interfere
      // with the previous SETTINGS() declared by hiding it with scoping.
      SETTINGS(structureStateFileName);
      bool stateFileIsValid =
        settings->value("structure/saveSuccessful", false).toBool();

      // Copy it over if it's a valid state file...
      if (stateFileIsValid) {
        if (QFile::exists(oldStructureStateFileName)) {
          QFile::remove(oldStructureStateFileName);
        }
        QFile::copy(structureStateFileName, oldStructureStateFileName);
      }
    }

    if (notify && m_dialog) {
      m_dialog->updateProgressLabel(
        tr("Saving: Writing %1...").arg(structureStateFileName));
    }
    structure->writeSettings(structureStateFileName);

    // Special request from Eva: if we are using VASP and we encounter
    // a structure that skipped optimization (primitive reduction, for
    // instance), still write the CONTCAR in the structure directory.
    if (structure->skippedOptimization() &&
        optimizer(getNumOptSteps() - 1)->getIDString() == "VASP") {
      QFile file(structure->fileName() + "/CONTCAR");
      file.open(QIODevice::WriteOnly);

      std::stringstream ss;
      PoscarFormat::write(*structure, ss);
      file.write(ss.str().c_str());
    }
  }

  /////////////////////////
  // Print results files //
  /////////////////////////

  // Only print the results file if we have a file path
  if (!filePath.isEmpty()) {
    QFile file(filePath + "/results.txt");
    QFile oldfile(filePath + "/results_old.txt");
    if (notify && m_dialog) {
      m_dialog->updateProgressLabel(
        tr("Saving: Writing %1...").arg(file.fileName()));
    }
    if (oldfile.open(QIODevice::ReadOnly))
      oldfile.remove();
    if (file.open(QIODevice::ReadOnly))
      file.copy(oldfile.fileName());
    file.close();
    if (!file.open(QIODevice::WriteOnly)) {
      error("OptBase::save(): Error opening file " + file.fileName() +
            " for writing...");
      return false;
    }
    QTextStream out(&file);

    QList<Structure*> sortedStructures;

    for (int i = 0; i < structures->size(); i++)
      sortedStructures.append(structures->at(i));
    if (sortedStructures.size() != 0) {
      Structure::sortAndRankByEnthalpy(&sortedStructures);
      out << sortedStructures.first()->getResultsHeader(m_calculateHardness)
          << endl;
    }

    for (int i = 0; i < sortedStructures.size(); i++) {
      structure = sortedStructures.at(i);
      if (!structure)
        continue; // In case there was a problem copying.
      QReadLocker structureLocker(&structure->lock());
      out << structure->getResultsEntry(m_calculateHardness) << endl;
      structureLocker.unlock();
      if (notify && m_dialog) {
        m_dialog->stopProgressUpdate();
      }
    }
  }

  // Write the user values to the output
  writeUserValuesToSettings(structureStateFileName.toStdString());

  // Write the template settings to the output file
  writeAllTemplatesToSettings(structureStateFileName.toStdString());

  // Mark operation successful
  settings->setValue(m_idString.toLower() + "/saveSuccessful", true);

  return true;
}

QString OptBase::interpretTemplate(const QString& str, Structure* structure)
{
  QStringList list = str.split("%");
  QString line;
  QString origLine;
  for (int line_ind = 0; line_ind < list.size(); line_ind++) {
    origLine = line = list.at(line_ind);
    interpretKeyword_base(line, structure);
    // Add other interpret keyword sections here if needed when subclassing
    if (line != origLine) { // Line was a keyword
      list.replace(line_ind, line);
    }
  }
  // Rejoin string
  QString ret = list.join("");
  ret += "\n";
  return ret;
}

void OptBase::interpretKeyword_base(QString& line, Structure* structure)
{
  QString rep = "";
  // User data
  if (line == "user1")
    rep += getUser1().c_str();
  else if (line == "user2")
    rep += getUser2().c_str();
  else if (line == "user3")
    rep += getUser3().c_str();
  else if (line == "user4")
    rep += getUser4().c_str();
  else if (line == "description")
    rep += description;
  else if (line == "percent")
    rep += "%";

  // Structure specific data
  if (line == "coords") {
    std::vector<GlobalSearch::Atom>& atoms = structure->atoms();
    std::vector<GlobalSearch::Atom>::const_iterator it;
    for (it = atoms.begin(); it != atoms.end(); it++) {
      rep += (QString(ElemInfo::getAtomicSymbol((*it).atomicNumber()).c_str()) +
              " ");
      const Vector3& vec = (*it).pos();
      rep += QString::number(vec.x()) + " ";
      rep += QString::number(vec.y()) + " ";
      rep += QString::number(vec.z()) + "\n";
    }
  } else if (line == "coordsInternalFlags") {
    std::vector<GlobalSearch::Atom>& atoms = structure->atoms();
    std::vector<GlobalSearch::Atom>::const_iterator it;
    for (it = atoms.begin(); it != atoms.end(); it++) {
      rep += (QString(ElemInfo::getAtomicSymbol((*it).atomicNumber()).c_str()) +
              " ");
      const Vector3& vec = (*it).pos();
      rep += QString::number(vec.x()) + " 1 ";
      rep += QString::number(vec.y()) + " 1 ";
      rep += QString::number(vec.z()) + " 1\n";
    }
  } else if (line == "coordsSuffixFlags") {
    std::vector<GlobalSearch::Atom>& atoms = structure->atoms();
    std::vector<GlobalSearch::Atom>::const_iterator it;
    for (it = atoms.begin(); it != atoms.end(); it++) {
      rep += (QString(ElemInfo::getAtomicSymbol((*it).atomicNumber()).c_str()) +
              " ");
      const Vector3& vec = (*it).pos();
      rep += QString::number(vec.x()) + " ";
      rep += QString::number(vec.y()) + " ";
      rep += QString::number(vec.z()) + " 1 1 1\n";
    }
  } else if (line == "coordsId") {
    std::vector<GlobalSearch::Atom>& atoms = structure->atoms();
    std::vector<GlobalSearch::Atom>::const_iterator it;
    for (it = atoms.begin(); it != atoms.end(); it++) {
      rep += (QString(ElemInfo::getAtomicSymbol((*it).atomicNumber()).c_str()) +
              " ");
      rep += QString::number((*it).atomicNumber()) + " ";
      const Vector3& vec = (*it).pos();
      rep += QString::number(vec.x()) + " ";
      rep += QString::number(vec.y()) + " ";
      rep += QString::number(vec.z()) + "\n";
    }
  } else if (line == "numAtoms")
    rep += QString::number(structure->numAtoms());
  else if (line == "numSpecies")
    rep += QString::number(structure->getSymbols().size());
  else if (line == "filename")
    rep += structure->fileName();
  else if (line == "rempath")
    rep += structure->getRempath();
  else if (line == "gen")
    rep += QString::number(structure->getGeneration());
  else if (line == "id")
    rep += QString::number(structure->getIDNumber());
  else if (line == "incar")
    rep += QString::number(structure->getCurrentOptStep());
  else if (line == "optStep")
    rep += QString::number(structure->getCurrentOptStep());
  else if (line.startsWith("filecontents:", Qt::CaseInsensitive)) {
    QString filename = line;
    filename.remove(0, QString("filecontents:").size());
    filename = filename.trimmed();
    // Attempt to open the file
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
      qDebug() << "Error in" << __FUNCTION__ << ": could not open" << filename;
    }
    rep += file.readAll();
  }
  // Append a file to be copied to the working dir
  else if (line.startsWith("copyfile:", Qt::CaseInsensitive)) {
    QString filename = line;
    filename.remove(0, QString("copyfile:").size());
    filename = filename.trimmed();
    structure->appendCopyFile(filename.toStdString());
    line = "";
  }

  if (!rep.isEmpty()) {
    // Remove any trailing newlines
    rep = rep.replace(QRegExp("\n$"), "");
    line = rep;
  }
}

QString OptBase::getTemplateKeywordHelp_base()
{
  QString str;
  QTextStream out(&str);
  out << "The following keywords should be used instead of the indicated "
         "variable data:\n"
      << "\n"
      << "Misc:\n"
      << "%fileContents:/path/to/local/file% -- Replaced with the contents of "
      << "the specified file\n"
      << "%copyFile:/path/to/local/file% -- Copy the specified file to the "
      << "structures's working directory\n"
      << "%percent% -- Literal percent sign (needed for CASTEP!)\n"
      << "\n"
      << "User data:\n"
      << "%userX% -- User specified value, where X = 1, 2, 3, or 4\n"
      << "%description% -- Optimization description\n"
      << "\n"
      << "Atomic coordinate formats for isolated structures:\n"
      << "%coords% -- cartesian coordinates\n\t[symbol] [x] [y] [z]\n"
      << "%coordsInternalFlags% -- cartesian coordinates; flag after each "
         "coordinate\n\t[symbol] [x] 1 [y] 1 [z] 1\n"
      << "%coordsSuffixFlags% -- cartesian coordinates; flags after all "
         "coordinates\n\t[symbol] [x] [y] [z] 1 1 1\n"
      << "%coordsId% -- cartesian coordinates with atomic number\n\t[symbol] "
         "[atomic number] [x] [y] [z]\n"
      << "\n"
      << "Generic structure data:\n"
      << "%numAtoms% -- Number of atoms in unit cell\n"
      << "%numSpecies% -- Number of unique atomic species in unit cell\n"
      << "%filename% -- local output filename\n"
      << "%rempath% -- path to structure's remote directory\n"
      << "%gen% -- structure generation number (if relevant)\n"
      << "%id% -- structure id number\n"
      << "%optStep% -- current optimization step\n";
  return str;
}

std::unique_ptr<QueueInterface> OptBase::createQueueInterface(
  const std::string& queueName)
{
  qDebug() << "Error:" << __FUNCTION__ << "not implemented. It needs to"
           << "be overridden in a derived class.";
  return nullptr;
}

std::unique_ptr<Optimizer> OptBase::createOptimizer(const std::string& optName)
{
  qDebug() << "Error:" << __FUNCTION__ << "not implemented. It needs to"
           << "be overridden in a derived class.";
  return nullptr;
}

QueueInterface* OptBase::queueInterface(int optStep) const
{
  if (optStep >= getNumOptSteps()) {
    qDebug() << "Error in" << __FUNCTION__ << ": optStep," << optStep
             << ", is out of bounds! The number of optimization steps is:"
             << getNumOptSteps();
    return nullptr;
  }
  return m_queueInterfaceAtOptStep[optStep].get();
}

int OptBase::queueInterfaceIndex(const QueueInterface* qi) const
{
  for (size_t i = 0; i < m_queueInterfaceAtOptStep.size(); ++i) {
    if (qi == m_queueInterfaceAtOptStep[i].get())
      return i;
  }
  return -1;
}

Optimizer* OptBase::optimizer(int optStep) const
{
  if (optStep >= getNumOptSteps()) {
    qDebug() << "Error in" << __FUNCTION__ << ": optStep," << optStep
             << ", is out of bounds! The number of optimization steps is:"
             << getNumOptSteps();
    return nullptr;
  }
  return m_optimizerAtOptStep[optStep].get();
}

int OptBase::optimizerIndex(const Optimizer* optimizer) const
{
  for (size_t i = 0; i < m_optimizerAtOptStep.size(); ++i) {
    if (optimizer == m_optimizerAtOptStep[i].get())
      return i;
  }
  return -1;
}

#ifdef ENABLE_MOLECULAR

long long OptBase::generateConformers()
{
  // First, try to open the sdf file
  std::ifstream sdfIstream(m_initialMolFile);

  if (!sdfIstream.is_open()) {
    std::cerr << "Error: failed to open initial SDF file: " << m_initialMolFile
              << "\n";
    return -1;
  }

  // Perform a few sanity checks
  if (m_numConformersToGenerate == 0) {
    std::cerr << "Error: " << __FUNCTION__ << " was asked to generate 0 "
              << "conformers.\n";
    return -1;
  }

  if (m_rmsdThreshold < 0.0) {
    std::cerr << "Error: rmsd threshold, " << m_rmsdThreshold << " is less "
              << "than zero!\n";
    return -1;
  }

  return ConformerGenerator::generateConformers(
    sdfIstream, m_conformerOutDir, m_numConformersToGenerate, m_maxOptIters,
    m_rmsdThreshold, m_pruneConfsAfterOpt);
}

#endif // ENABLE_MOLECULAR

void OptBase::clearOptSteps()
{
  m_queueInterfaceAtOptStep.clear();
  m_optimizerAtOptStep.clear();
  m_queueInterfaceTemplates.clear();
  m_optimizerTemplates.clear();
  m_numOptSteps = 0;
}

void OptBase::appendOptStep()
{
  // If there are no opt steps, we can't copy previous ones
  if (m_numOptSteps == 0) {
    typedef std::map<std::string, std::string> templateMap;
    m_queueInterfaceAtOptStep.push_back(nullptr);
    m_optimizerAtOptStep.push_back(nullptr);
    m_queueInterfaceTemplates.push_back(templateMap());
    m_optimizerTemplates.push_back(templateMap());
  }
  // We will duplicate the most recent opt step otherwise
  else {
    m_queueInterfaceAtOptStep.push_back(createQueueInterface(
      m_queueInterfaceAtOptStep.back()->getIDString().toStdString()));
    m_optimizerAtOptStep.push_back(createOptimizer(
      m_optimizerAtOptStep.back()->getIDString().toStdString()));
    m_queueInterfaceTemplates.push_back(m_queueInterfaceTemplates.back());
    m_optimizerTemplates.push_back(m_optimizerTemplates.back());
  }

  ++m_numOptSteps;
}

void OptBase::insertOptStep(size_t optStep)
{
  // If we are adding an opt step to the end, just use the append function
  if (optStep == m_numOptSteps) {
    appendOptStep();
    return;
  }

  if (optStep > m_numOptSteps) {
    qDebug() << "Error in" << __FUNCTION__ << ": attempting to insert"
             << "an opt step," << optStep << ", that is greater than"
             << "the number of opt steps," << m_numOptSteps;
    return;
  }

  // We will copy another step. Figure out the index of the one we will copy.
  // We will copy the one immediately prior to optStep in most cases, but
  // if optStep is 0, we will copy the first item already present
  size_t copyInd = (optStep == 0 ? 0 : optStep - 1);
  m_queueInterfaceAtOptStep.insert(
    m_queueInterfaceAtOptStep.begin() + optStep,
    createQueueInterface(
      m_queueInterfaceAtOptStep[copyInd]->getIDString().toStdString()));
  m_optimizerAtOptStep.insert(
    m_optimizerAtOptStep.begin() + optStep,
    createOptimizer(
      m_optimizerAtOptStep[copyInd]->getIDString().toStdString()));

  m_queueInterfaceTemplates.insert(m_queueInterfaceTemplates.begin() + optStep,
                                   m_queueInterfaceTemplates[copyInd]);

  m_optimizerTemplates.insert(m_optimizerTemplates.begin() + optStep,
                              m_optimizerTemplates[copyInd]);

  ++m_numOptSteps;
}

void OptBase::removeOptStep(size_t optStep)
{
  if (optStep >= m_numOptSteps) {
    qDebug() << "Error in" << __FUNCTION__ << ": attempting to remove"
             << "an opt step," << optStep << ", that is out of bounds.\n"
             << "The number of opt steps is" << m_numOptSteps;
    return;
  }

  m_queueInterfaceAtOptStep.erase(m_queueInterfaceAtOptStep.begin() + optStep);
  m_optimizerAtOptStep.erase(m_optimizerAtOptStep.begin() + optStep);

  m_queueInterfaceTemplates.erase(m_queueInterfaceTemplates.begin() + optStep);
  m_optimizerTemplates.erase(m_optimizerTemplates.begin() + optStep);

  --m_numOptSteps;
}

void OptBase::setQueueInterface(size_t optStep, const std::string& qiName)
{
  if (optStep >= m_numOptSteps) {
    qDebug() << "Error in" << __FUNCTION__ << ": optStep," << optStep
             << ", is out of bounds. Number of opt steps is" << m_numOptSteps;
    return;
  }
  m_queueInterfaceAtOptStep[optStep] = createQueueInterface(qiName);

  // We need to populate the templates list with empty templates
  for (const auto& templateName :
       m_queueInterfaceAtOptStep[optStep]->getTemplateFileNames()) {
    setQueueInterfaceTemplate(optStep, templateName.toStdString(), "");
  }
}

std::string OptBase::getQueueInterfaceTemplate(size_t optStep,
                                               const std::string& name) const
{
  if (optStep >= m_numOptSteps) {
    qDebug() << "Error in" << __FUNCTION__ << ": optStep," << optStep
             << ", is out of bounds. Number of opt steps is" << m_numOptSteps;
    return "";
  }
  if (m_queueInterfaceTemplates[optStep].count(name) == 0) {
    qDebug() << "Error in" << __FUNCTION__ << ": invalid key entry"
             << "Name:" << name.c_str() << ", for opt step:" << optStep;
    return "";
  }
  return m_queueInterfaceTemplates[optStep].at(name);
}

void OptBase::setQueueInterfaceTemplate(size_t optStep, const std::string& name,
                                        const std::string& temp)
{
  if (optStep >= m_numOptSteps) {
    qDebug() << "Error in" << __FUNCTION__ << ": optStep," << optStep
             << ", is out of bounds. Number of opt steps is" << m_numOptSteps;
    return;
  }
  m_queueInterfaceTemplates[optStep][name] = temp;
}

void OptBase::setOptimizer(size_t optStep, const std::string& optName)
{
  if (optStep >= m_numOptSteps) {
    qDebug() << "Error in" << __FUNCTION__ << ": optStep," << optStep
             << ", is out of bounds. Number of opt steps is" << m_numOptSteps;
    return;
  }
  m_optimizerAtOptStep[optStep] = createOptimizer(optName);

  // We need to populate the templates list with empty templates
  for (const auto& templateName :
       m_optimizerAtOptStep[optStep]->getTemplateFileNames()) {
    setOptimizerTemplate(optStep, templateName.toStdString(), "");
  }
}

std::string OptBase::getOptimizerTemplate(size_t optStep,
                                          const std::string& name) const
{
  if (optStep >= m_numOptSteps) {
    qDebug() << "Error in" << __FUNCTION__ << ": optStep," << optStep
             << ", is out of bounds. Number of opt steps is" << m_numOptSteps;
    return "";
  }
  if (m_optimizerTemplates[optStep].count(name) == 0) {
    qDebug() << "Error in" << __FUNCTION__ << ": invalid key entry"
             << "Name:" << name.c_str() << ", for opt step:" << optStep;
    return "";
  }
  return m_optimizerTemplates[optStep].at(name);
}

void OptBase::setOptimizerTemplate(size_t optStep, const std::string& name,
                                   const std::string& temp)
{
  if (optStep >= m_numOptSteps) {
    qDebug() << "Error in" << __FUNCTION__ << ": optStep," << optStep
             << ", is out of bounds. Number of opt steps is" << m_numOptSteps;
    return;
  }
  m_optimizerTemplates[optStep][name] = temp;
}

OptBase::TemplateType OptBase::getTemplateType(size_t optStep,
                                               const std::string& name) const
{
  TemplateType ret = TT_Unknown;

  if (optStep >= m_numOptSteps) {
    qDebug() << "Error in" << __FUNCTION__ << ": optStep," << optStep
             << ", is out of range! Num opt steps is: " << m_numOptSteps;
    return ret;
  }

  if (queueInterface(optStep) &&
      queueInterface(optStep)->isTemplateFileName(name.c_str())) {
    if (ret != TT_Unknown) {
      qDebug() << "Error: in" << __FUNCTION__ << ": template name,"
               << name.c_str() << ", in multiple template types!";
      ret = TT_Unknown;
      return ret;
    }
    ret = TT_QueueInterface;
  }

  if (optimizer(optStep) &&
      optimizer(optStep)->isTemplateFileName(name.c_str())) {
    if (ret != TT_Unknown) {
      qDebug() << "Error: in" << __FUNCTION__ << ": template name,"
               << name.c_str() << ", in multiple template types!";
      ret = TT_Unknown;
      return ret;
    }
    ret = TT_Optimizer;
  }

  if (ret == TT_Unknown) {
    qDebug() << "Error in" << __FUNCTION__
             << ": unknown template type: " << name.c_str();
  }

  return ret;
}

std::string OptBase::getTemplate(size_t optStep, const std::string& name) const
{
  TemplateType type = getTemplateType(optStep, name);

  if (type == TT_Unknown)
    return "";

  if (type == TT_QueueInterface)
    return getQueueInterfaceTemplate(optStep, name);

  if (type == TT_Optimizer)
    return getOptimizerTemplate(optStep, name);

  // We should never make it here
  return "";
}

void OptBase::setTemplate(size_t optStep, const std::string& name,
                          const std::string& temp)
{
  TemplateType type = getTemplateType(optStep, name);

  if (type == TT_Unknown)
    return;

  if (type == TT_QueueInterface)
    setQueueInterfaceTemplate(optStep, name, temp);

  if (type == TT_Optimizer)
    setOptimizerTemplate(optStep, name, temp);

  // We should never make it here
}

void OptBase::readQueueInterfaceTemplatesFromSettings(
  size_t optStep, const std::string& settingsFile)
{
  QueueInterface* queue = queueInterface(optStep);
  if (!queue) {
    qDebug() << "Error in " << __FUNCTION__ << ": queue interface at"
             << "opt step" << optStep << "does not exist!";
    return;
  }

  SETTINGS(settingsFile.c_str());

  settings->beginGroup(getIDString().toLower() + "/edit/queueInterface/" +
                       QString::number(optStep) + "/" +
                       queue->getIDString().toLower());
  QStringList filenames = queue->getTemplateFileNames();
  for (const auto& filename : filenames) {
    QString temp = settings->value(filename).toString();
    setQueueInterfaceTemplate(optStep, filename.toStdString(),
                              temp.toStdString());
  }
  settings->endGroup();
}

void OptBase::readOptimizerTemplatesFromSettings(
  size_t optStep, const std::string& settingsFile)
{
  Optimizer* optim = optimizer(optStep);
  if (!optim) {
    qDebug() << "Error in " << __FUNCTION__ << ": optimizer at"
             << "opt step" << optStep << "does not exist!";
    return;
  }

  SETTINGS(settingsFile.c_str());

  settings->beginGroup(getIDString().toLower() + "/edit/optimizer/" +
                       QString::number(optStep) + "/" +
                       optim->getIDString().toLower());
  QStringList filenames = optim->getTemplateFileNames();
  for (const auto& filename : filenames) {
    QString temp = settings->value(filename).toString();

    setOptimizerTemplate(optStep, filename.toStdString(), temp.toStdString());

    if (!temp.isEmpty())
      continue;

    // If "temp" is empty, perhaps we have some template filenames to open
    QString templateFile = settings->value(filename + "_templates").toString();

    if (templateFile.isEmpty())
      continue;

    QFile file(templateFile);

    // If the file exists, store it in the templates
    if (!file.exists()) {
      qWarning() << "Warning in " << __FUNCTION__ << ": " << templateFile
                 << "does not exist!";
      continue;
    }
    if (!file.open(QIODevice::ReadOnly)) {
      qWarning() << "Warning in " << __FUNCTION__ << ": " << templateFile
                 << "could not be opened!";
      continue;
    }

    setOptimizerTemplate(optStep, filename.toStdString(),
                         QString(file.readAll()).toStdString());
    file.close();
  }
  settings->endGroup();
}

void OptBase::readTemplatesFromSettings(size_t optStep,
                                        const std::string& filename)
{
  readQueueInterfaceTemplatesFromSettings(optStep, filename);
  readOptimizerTemplatesFromSettings(optStep, filename);
}

void OptBase::readAllTemplatesFromSettings(const std::string& filename)
{
  SETTINGS(filename.c_str());
  settings->beginGroup(getIDString().toLower() + "/edit");
  size_t numOptSteps = settings->value("numOptSteps").toUInt();
  while (getNumOptSteps() < numOptSteps)
    appendOptStep();

  for (size_t i = 0; i < getNumOptSteps(); ++i)
    readTemplatesFromSettings(i, filename);
  settings->endGroup();
}

void OptBase::writeQueueInterfaceTemplatesToSettings(
  size_t optStep, const std::string& settingsFilename)
{
  QueueInterface* queue = queueInterface(optStep);
  if (!queue) {
    qDebug() << "Error in " << __FUNCTION__ << ": queue interface at"
             << "opt step" << optStep << "does not exist!";
    return;
  }

  SETTINGS(settingsFilename.c_str());
  // QueueInterface templates
  settings->beginGroup(getIDString().toLower() + "/edit/queueInterface/" +
                       QString::number(optStep) + "/" +
                       queue->getIDString().toLower());

  QStringList filenames = queue->getTemplateFileNames();
  for (const auto& filename : filenames) {
    settings->setValue(
      filename,
      getQueueInterfaceTemplate(optStep, filename.toStdString()).c_str());
  }
  settings->endGroup();
}

void OptBase::writeOptimizerTemplatesToSettings(
  size_t optStep, const std::string& settingsFilename)
{
  Optimizer* optim = optimizer(optStep);
  if (!optim) {
    qDebug() << "Error in " << __FUNCTION__ << ": optimizer at"
             << "opt step" << optStep << "does not exist!";
    return;
  }

  SETTINGS(settingsFilename.c_str());
  // Optimizer templates
  settings->beginGroup(getIDString().toLower() + "/edit/optimizer/" +
                       QString::number(optStep) + "/" +
                       optim->getIDString().toLower());

  QStringList filenames = optim->getTemplateFileNames();
  for (const auto& filename : filenames) {
    settings->setValue(
      filename, getOptimizerTemplate(optStep, filename.toStdString()).c_str());
  }
  settings->endGroup();
}

void OptBase::writeTemplatesToSettings(size_t optStep,
                                       const std::string& filename)
{
  writeQueueInterfaceTemplatesToSettings(optStep, filename);
  writeOptimizerTemplatesToSettings(optStep, filename);
}

void OptBase::writeAllTemplatesToSettings(const std::string& filename)
{
  SETTINGS(filename.c_str());
  settings->beginGroup(getIDString().toLower() + "/edit");
  settings->setValue("numOptSteps", QString::number(getNumOptSteps()));
  for (size_t i = 0; i < getNumOptSteps(); ++i)
    writeTemplatesToSettings(i, filename);
  settings->endGroup();
}

void OptBase::readUserValuesFromSettings(const std::string& filename)
{
  SETTINGS(filename.c_str());

  settings->beginGroup(getIDString().toLower() + "/edit");
  m_user1 = settings->value("/user1", "").toString().toStdString();
  m_user2 = settings->value("/user2", "").toString().toStdString();
  m_user3 = settings->value("/user3", "").toString().toStdString();
  m_user4 = settings->value("/user4", "").toString().toStdString();
  settings->endGroup();
}

void OptBase::writeUserValuesToSettings(const std::string& filename)
{
  SETTINGS(filename.c_str());

  settings->beginGroup(getIDString().toLower() + "/edit");

  settings->setValue("/user1", m_user1.c_str());
  settings->setValue("/user2", m_user2.c_str());
  settings->setValue("/user3", m_user3.c_str());
  settings->setValue("/user4", m_user4.c_str());

  settings->endGroup();
}

bool OptBase::isReadyToSearch(QString& err) const
{
  err.clear();
  if (filePath.isEmpty()) {
    err += "Local working directory is not set.";
    return false;
  }

  for (size_t i = 0; i < getNumOptSteps(); ++i) {
    if (!queueInterface(i)) {
      err += "Queue interface at opt step " + QString::number(i + 1) +
             " is not set!";
      return false;
    }

    if (!optimizer(i)) {
      err += "Optimizer at opt step " + QString::number(i + 1) + " is not set!";
      return false;
    }

    if (!queueInterface(i)->isReadyToSearch(&err))
      return false;

    if (!optimizer(i)->isReadyToSearch(&err))
      return false;
  }

  return true;
}

bool OptBase::anyRemoteQueueInterfaces() const
{
  for (size_t i = 0; i < getNumOptSteps(); ++i) {
    if (queueInterface(i)->getIDString().toLower() != "local")
      return true;
  }
  return false;
}

void OptBase::promptForPassword(const QString& message, QString* newPassword,
                                bool* ok)
{
  if (m_usingGUI) {
    (*newPassword) = QInputDialog::getText(dialog(), "Need password:", message,
                                           QLineEdit::Password, QString(), ok);
  } else {
    (*newPassword) = PasswordPrompt::getPassword().c_str();
  }
}

void OptBase::promptForBoolean(const QString& message, bool* ok)
{
  if (m_usingGUI) {
    if (QMessageBox::question(dialog(), m_idString, message,
                              QMessageBox::Yes | QMessageBox::No) ==
        QMessageBox::Yes) {
      *ok = true;
    } else {
      *ok = false;
    }
  } else {
    std::cout << message.toStdString() << "\n[y/N]\n";
    std::string in;
    std::getline(std::cin, in);
    in = trim(in);
    if (in.size() != 0 && (in[0] == 'y' || in[0] == 'Y'))
      *ok = true;
    else
      *ok = false;
  }
}

void OptBase::setClipboard(const QString& text) const
{
  emit sig_setClipboard(text);
}

// No need to document this
/// @cond
void OptBase::setClipboard_(const QString& text) const
{
  // Set to system clipboard
  QApplication::clipboard()->setText(text, QClipboard::Clipboard);
  // For middle-click on X11
  if (QApplication::clipboard()->supportsSelection()) {
    QApplication::clipboard()->setText(text, QClipboard::Selection);
  }
}
/// @endcond

#ifdef ENABLE_SSH
#ifndef USE_CLI_SSH

bool OptBase::createSSHConnections_libssh()
{
  delete m_ssh;
  SSHManagerLibSSH* libsshManager = new SSHManagerLibSSH(5, this);
  m_ssh = libsshManager;
  QString pw = "";
  for (;;) {
    try {
      libsshManager->makeConnections(host, username, pw, port);
    } catch (SSHConnection::SSHConnectionException e) {
      QString err;
      switch (e) {
        case SSHConnection::SSH_CONNECTION_ERROR:
        case SSHConnection::SSH_UNKNOWN_ERROR:
        default:
          err = "There was a problem connection to the ssh server at " +
                username + "@" + host + ":" + QString::number(port) +
                ". "
                "Please check that all provided information is correct, and "
                "attempt to log in outside of Avogadro before trying again.";
          error(err);
          return false;
        case SSHConnection::SSH_UNKNOWN_HOST_ERROR: {
          // The host is not known, or has changed its key.
          // Ask user if this is ok.
          err = "The host " + host + ":" + QString::number(port) +
                " either has an unknown key, or has changed it's key:\n" +
                libsshManager->getServerKeyHash() + "\n" +
                "Would you like to trust the specified host?";
          bool ok;
          // This is a BlockingQueuedConnection, which blocks until
          // the slot returns.
          emit needBoolean(err, &ok);
          if (!ok) { // user cancels
            return false;
          }
          libsshManager->validateServerKey();
          continue;
        } // end case
        case SSHConnection::SSH_BAD_PASSWORD_ERROR: {
          // Chances are that the pubkey auth was attempted but failed,
          // so just prompt user for password.
          err = "Please enter a password for " + username + "@" + host + ":" +
                QString::number(port) + ": ";
          bool ok;
          QString newPassword;

          if (m_usingGUI)
            // This is a BlockingQueuedConnection, which blocks until
            // the slot returns.
            emit needPassword(err, &newPassword, &ok);
          else {
            newPassword =
              PasswordPrompt::getPassword(err.toStdString()).c_str();
            ok = true;
          }

          if (!ok) { // user cancels
            return false;
          }
          pw = newPassword;
          continue;
        } // end case
      }   // end switch
    }     // end catch
    break;
  } // end forever
  return true;
}

#else // not USE_CLI_SSH

bool OptBase::createSSHConnections_cli()
{
  // Since we rely on public key auth here, it's much easier to set up:
  SSHManagerCLI* cliSSHManager = new SSHManagerCLI(5, this);
  cliSSHManager->makeConnections(host, username, "", port);
  m_ssh = cliSSHManager;
  return true;
}

#endif // not USE_CLI_SSH
#endif // ENABLE_SSH

void OptBase::warning(const QString& s)
{
  qWarning() << "Warning: " << s;
  emit warningStatement(s);
}

void OptBase::debug(const QString& s)
{
  qDebug() << "Debug: " << s;
  emit debugStatement(s);
}

void OptBase::error(const QString& s)
{
  qWarning() << "Error: " << s;
  emit errorStatement(s);
}

void OptBase::message(const QString& s)
{
  qDebug() << "Message: " << s;
  emit messageStatement(s);
}

} // end namespace GlobalSearch
