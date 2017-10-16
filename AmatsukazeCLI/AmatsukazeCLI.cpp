/**
* Amtasukaze CLI Entry point
* Copyright (c) 2017 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/

__declspec(dllimport) int AmatsukazeCLI(int argc, const wchar_t* argv[]);

int wmain(int argc, const wchar_t* argv[]) {
  return AmatsukazeCLI(argc, argv);
}
