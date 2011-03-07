/**
 * \file ug.h
 */
// created by Sebastian Reiter
// s.b.reiter@googlemail.com
// y10 m05 d31

#ifndef __H__UG__UG__
#define __H__UG__UG__
#include <string>

#include "common/profiler/profiler.h"

/**
 * 	\brief the ug namespace
 *
 * Namespace for ug
 */
namespace ug
{

////////////////////////////////////////////////////////////////////////
///	initializes ug
/**	This method should be called at the beginning of main(...).
 *	If ug has been compiled for parallel use (UG_PARALLEL is defined)
 *	then this method will internally call pcl::Init.
 */
//int UGInit(int argc, char* argv[], int parallelOutputProcRank = -1);
int UGInit(int *argcp, char ***argvp, int parallelOutputProcRank = -1);

/// returns the ug app path
const std::string& UGGetApplicationPath();

/// returns the ug script path
const std::string& UGGetScriptPath();

/// returns the ug data path
const std::string& UGGetDataPath();

////////////////////////////////////////////////////////////////////////
///	finalizes ug
/**	If ug has been compiled for parallel use (UG_PARALLEL is defined)
 *	then this method will internally call pcl::Finalize.
 */
int UGFinalize(bool outputProfilerStats = false);

////////////////////////////////////////////////////////////////////////
/**	searches argv for the given parameter and returns its position in argv.
 *	If the parameter is not contained in argv, -1 is returned.
 */
int GetParamIndex(const char* param, int argc, char* argv[]);

////////////////////////////////////////////////////////////////////////
/**	searches argv for the given parameter and returns true if it is found.
 */
bool FindParam(const char* param, int argc, char* argv[]);

////////////////////////////////////////////////////////////////////////
/**	searches argv for the given parameter, and converts the
 *	associated value to an integer. Returns true if the parameter was
 *	found, false if not.
 */
bool ParamToInt(int& iOut, const char* param, int argc, char* argv[]);

////////////////////////////////////////////////////////////////////////
/**	searches argv for the given parameter, and returns the
 *	associated string (the argv directly following param).
 *	associated Returns true if the parameter was found, false if not.
 *
 *	Please note that spaces may not be contained in the associated string.
 */
bool ParamToString(char** strOut, const char* param, int argc, char* argv[]);


}//	end of namespace

#endif
