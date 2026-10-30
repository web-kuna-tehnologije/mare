extern bool (*vfs_mount)(const char *pathfile); // only .zip supported at the moment (extern: a C++ TU has no tentative definitions)

#if CODE
#pragma once
#include v3_vfs_backend

bool (*vfs_mount)(const char *pathfile) = vfs_mount_;
#endif
