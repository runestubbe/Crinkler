// Stand-in for the MFC resource header that Visual Studio's resource editor
// pulls into generated .rc files. windres has no MFC, but everything Crinkler's
// resource script actually needs lives in winresrc.h.
#pragma once

#include <winresrc.h>

#ifndef IDC_STATIC
#define IDC_STATIC (-1)
#endif
