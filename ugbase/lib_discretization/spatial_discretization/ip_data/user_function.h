/*
 * user_funtion.h
 *
 *  Created on: 17.12.2010
 *      Author: andreasvogel
 */

#ifndef __H__UG__LIB_DISCRETIZATION__SPATIAL_DISCRETIZATION__IP_DATA__USER_FUNCTION__
#define __H__UG__LIB_DISCRETIZATION__SPATIAL_DISCRETIZATION__IP_DATA__USER_FUNCTION__

namespace ug{

template <typename TData, typename TDataIn = TData>
class IFunction
{
	public:
	///	evaluates the data
		virtual void operator() (TData& out, int numArgs, ...) = 0;

	///	virtual destructor
		virtual ~IFunction() {}
};

} // end namespace ug

#endif /* __H__UG__LIB_DISCRETIZATION__SPATIAL_DISCRETIZATION__IP_DATA__USER_FUNCTION__ */
