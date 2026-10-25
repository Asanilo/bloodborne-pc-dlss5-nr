// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later
// Reuse the reviewed BBSC F6 input/ownership implementation without linking the
// network module. Namespace isolation prevents ELF interposition with
// libbbnet20.
#define BBNET_PORT_VERSION 1
#define Bbsc BbPortCinematic
#include "../bbnet20/bbsc/core/bbsc_cinematic.cpp"
#undef Bbsc
#undef BBNET_PORT_VERSION
