# Written by Patrick Avery - 2018
# This is a macro for installing a Qt style during a build
# Currently, it is only used for Apple and qmacstyle

macro(InstallQMacStyle _destination _qt_plugins_var)
  include(MacroInstallQt5Plugin)
  install_qt5_plugin("Qt5::QMacStylePlugin"
                     "${_destination}" "${_qt_plugins_var}")
endmacro()
