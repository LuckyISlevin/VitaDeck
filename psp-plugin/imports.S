# SystemCtrlForKernel imports of Adrenaline's SystemControl. NIDs are name hashes:
# the first four bytes of SHA-1(function name), little endian.
	.set noreorder

#include "pspimport.s"

	IMPORT_START "SystemCtrlForKernel",0x00090000
	IMPORT_FUNC  "SystemCtrlForKernel",0x159AF5CC,sctrlHENFindFunction
	IMPORT_FUNC  "SystemCtrlForKernel",0x2D10FB28,sctrlKernelLoadExecVSHWithApitype
	IMPORT_FUNC  "SystemCtrlForKernel",0x1C90BECB,sctrlHENSetStartModuleHandler
