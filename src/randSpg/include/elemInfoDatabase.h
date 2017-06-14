/**********************************************************************
  elemInfoDatabase.h - Database containing info about atomic symbols and radii

  Copyright (C) 2015 - 2016 by Patrick S. Avery

  This source code is released under the New BSD License, (the "License").

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

 ***********************************************************************/

#ifndef ELEM_INFO_DATABASE_H
#define ELEM_INFO_DATABASE_H

#include <string>
#include <vector>

namespace ElemInfoDatabase {
  // Atomic symbols
#if (defined(_MSC_VER) && _MSC_VER <= 1600)
  // Visual studio 2010 and earlier can't initialize vectors as easily
  static const char* _tmp_atomicSymbols[] =
#else
  // This is default
  static const std::vector<std::string> _atomicSymbols =
#endif
  {
    " ",  // 0 -- not a real element...
    "H",  // 1
    "He", // 2
    "Li", // 3
    "Be", // 4
    "B",  // 5
    "C",  // 6
    "N",  // 7
    "O",  // 8
    "F",  // 9
    "Ne", // 10
    "Na", // 11
    "Mg", // 12
    "Al", // 13
    "Si", // 14
    "P",  // 15
    "S",  // 16
    "Cl", // 17
    "Ar", // 18
    "K",  // 19
    "Ca", // 20
    "Sc", // 21
    "Ti", // 22
    "V",  // 23
    "Cr", // 24
    "Mn", // 25
    "Fe", // 26
    "Co", // 27
    "Ni", // 28
    "Cu", // 29
    "Zn", // 30
    "Ga", // 31
    "Ge", // 32
    "As", // 33
    "Se", // 34
    "Br", // 35
    "Kr", // 36
    "Rb", // 37
    "Sr", // 38
    "Y",  // 39
    "Zr", // 40
    "Nb", // 41
    "Mo", // 42
    "Tc", // 43
    "Ru", // 44
    "Rh", // 45
    "Pd", // 46
    "Ag", // 47
    "Cd", // 48
    "In", // 49
    "Sn", // 50
    "Sb", // 51
    "Te", // 52
    "I",  // 53
    "Xe", // 54
    "Cs", // 55
    "Ba", // 56
    "La", // 57
    "Ce", // 58
    "Pr", // 59
    "Nd", // 60
    "Pm", // 61
    "Sm", // 62
    "Eu", // 63
    "Gd", // 64
    "Tb", // 65
    "Dy", // 66
    "Ho", // 67
    "Er", // 68
    "Tm", // 69
    "Yb", // 70
    "Lu", // 71
    "Hf", // 72
    "Ta", // 73
    "W",  // 74
    "Re", // 75
    "Os", // 76
    "Ir", // 77
    "Pt", // 78
    "Au", // 79
    "Hg", // 80
    "Tl", // 81
    "Pb", // 82
    "Bi", // 83
    "Po", // 84
    "At", // 85
    "Rn", // 86
    "Fr", // 87
    "Ra", // 88
    "Ac", // 89
    "Th", // 90
    "Pa", // 91
    "U",  // 92
    "Np", // 93
    "Pu", // 94
    "Am", // 95
    "Cm", // 96
    "Bk", // 97
    "Cf", // 98
    "Es", // 99
    "Fm", // 100
    "Md", // 101
    "No", // 102
    "Lr", // 103
    "Rf", // 104
    "Db", // 105
    "Sg", // 106
    "Bh", // 107
    "Hs", // 108
    "Mt", // 109
    "Ds", // 110
    "Rg", // 111
    "Cn", // 112
    "Uut",// 113
    "Fl", // 114
    "Uup",// 115
    "Lv", // 116
    "Uus",// 117
  };
#if (defined(_MSC_VER) && _MSC_VER <= 1600)
  // Visual studio 2010 and earlier can't initialize vectors as easily
  static const std::vector<std::string> _atomicSymbols(_tmp_atomicSymbols, std::end(_tmp_atomicSymbols));
#endif

  // The following values were obtained from latest version of OpenBabel on
  // GitHub on 01/14/2016

#if (defined(_MSC_VER) && _MSC_VER <= 1600)
  // Visual studio 2010 and earlier can't initialize vectors as easily
  static const double _tmp_covalentRadii[] =
#else
  static const std::vector<double> _covalentRadii =
#endif
  {
    0,    // 0 - not a real element...
    0.31, // 1
    0.28, // 2
    1.28, // 3
    0.96, // 4
    0.84, // 5
    0.76, // 6
    0.71, // 7
    0.66, // 8
    0.57, // 9
    0.58, // 10
    1.66, // 11
    1.41, // 12
    1.21, // 13
    1.11, // 14
    1.07, // 15
    1.05, // 16
    1.02, // 17
    1.06, // 18
    2.03, // 19
    1.76, // 20
    1.7,  // 21
    1.6,  // 22
    1.53, // 23
    1.39, // 24
    1.39, // 25
    1.32, // 26
    1.26, // 27
    1.24, // 28
    1.32, // 29
    1.22, // 30
    1.22, // 31
    1.2,  // 32
    1.19, // 33
    1.2,  // 34
    1.2,  // 35
    1.16, // 36
    2.2,  // 37
    1.95, // 38
    1.9,  // 39
    1.75, // 40
    1.64, // 41
    1.54, // 42
    1.47, // 43
    1.46, // 44
    1.42, // 45
    1.39, // 46
    1.45, // 47
    1.44, // 48
    1.42, // 49
    1.39, // 50
    1.39, // 51
    1.38, // 52
    1.39, // 53
    1.4,  // 54
    2.44, // 55
    2.15, // 56
    2.07, // 57
    2.04, // 58
    2.03, // 59
    2.01, // 60
    1.99, // 61
    1.98, // 62
    1.98, // 63
    1.96, // 64
    1.94, // 65
    1.92, // 66
    1.92, // 67
    1.89, // 68
    1.9,  // 69
    1.87, // 70
    1.87, // 71
    1.75, // 72
    1.7,  // 73
    1.62, // 74
    1.51, // 75
    1.44, // 76
    1.41, // 77
    1.36, // 78
    1.36, // 79
    1.32, // 80
    1.45, // 81
    1.46, // 82
    1.48, // 83
    1.4,  // 84
    1.5,  // 85
    1.5,  // 86
    2.6,  // 87
    2.21, // 88
    2.15, // 89
    2.06, // 90
    2,    // 91
    1.96, // 92
    1.9,  // 93
    1.87, // 94
    1.8,  // 95
    1.69, // 96
    1.6,  // 97
    1.6,  // 98
    1.6,  // 99
    1.6,  // 100
    1.6,  // 101
    1.6,  // 102
    1.6,  // 103
    1.6,  // 104
    1.6,  // 105
    1.6,  // 106
    1.6,  // 107
    1.6,  // 108
    1.6,  // 109
    1.6,  // 110
    1.6,  // 111
    1.6,  // 112
    1.6,  // 113
    1.6,  // 114
    1.6,  // 115
    1.6,  // 116
    1.6,  // 117
  };
#if (defined(_MSC_VER) && _MSC_VER <= 1600)
  // Visual studio 2010 and earlier can't initialize vectors as easily
  static const std::vector<double> _covalentRadii(_tmp_covalentRadii, std::end(_tmp_covalentRadii));
#endif

  // The following values were obtained from latest version of OpenBabel on
  // GitHub on 01/14/2016

#if (defined(_MSC_VER) && _MSC_VER <= 1600)
  // Visual studio 2010 and earlier can't initialize vectors as easily
  static const double _tmp_vdwRadii[] =
#else
  static const std::vector<double> _vdwRadii =
#endif
  {
    0,    // 0 - not a real element...
    1.1,  // 1
    1.4,  // 2
    1.81, // 3
    1.53, // 4
    1.92, // 5
    1.7,  // 6
    1.55, // 7
    1.52, // 8
    1.47, // 9
    1.54, // 10
    2.27, // 11
    1.73, // 12
    1.84, // 13
    2.1,  // 14
    1.8,  // 15
    1.8,  // 16
    1.75, // 17
    1.88, // 18
    2.75, // 19
    2.31, // 20
    2.3,  // 21
    2.15, // 22
    2.05, // 23
    2.05, // 24
    2.05, // 25
    2.05, // 26
    2,    // 27
    2,    // 28
    2,    // 29
    2.1,  // 30
    1.87, // 31
    2.11, // 32
    1.85, // 33
    1.9,  // 34
    1.83, // 35
    2.02, // 36
    3.03, // 37
    2.49, // 38
    2.4,  // 39
    2.3,  // 40
    2.15, // 41
    2.1,  // 42
    2.05, // 43
    2.05, // 44
    2,    // 45
    2.05, // 46
    2.1,  // 47
    2.2,  // 48
    2.2,  // 49
    1.93, // 50
    2.17, // 51
    2.06, // 52
    1.98, // 53
    2.16, // 54
    3.43, // 55
    2.68, // 56
    2.5,  // 57
    2.48, // 58
    2.47, // 59
    2.45, // 60
    2.43, // 61
    2.42, // 62
    2.4,  // 63
    2.38, // 64
    2.37, // 65
    2.35, // 66
    2.33, // 67
    2.32, // 68
    2.3,  // 69
    2.28, // 70
    2.27, // 71
    2.25, // 72
    2.2,  // 73
    2.1,  // 74
    2.05, // 75
    2,    // 76
    2,    // 77
    2.05, // 78
    2.1,  // 79
    2.05, // 80
    1.96, // 81
    2.02, // 82
    2.07, // 83
    1.97, // 84
    2.02, // 85
    2.2,  // 86
    3.48, // 87
    2.83, // 88
    2,    // 89
    2.4,  // 90
    2,    // 91
    2.3,  // 92
    2,    // 93
    2,    // 94
    2,    // 95
    2,    // 96
    2,    // 97
    2,    // 98
    2,    // 99
    2,    // 100
    2,    // 101
    2,    // 102
    2,    // 103
    2,    // 104
    2,    // 105
    2,    // 106
    2,    // 107
    2,    // 108
    2,    // 109
    2,    // 110
    2,    // 111
    2,    // 112
    2,    // 113
    2,    // 114
    2,    // 115
    2,    // 116
    2,    // 117
  };
#if (defined(_MSC_VER) && _MSC_VER <= 1600)
  // Visual studio 2010 and earlier can't initialize vectors as easily
  static const std::vector<double> _vdwRadii(_tmp_vdwRadii, std::end(_tmp_vdwRadii));
#endif
} // namespace ElemInfoDatabase

#endif
