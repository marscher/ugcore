/*
 * elem_disc_assemble_util.h
 *
 *  Created on: 08.07.2010
 *      Author: andreasvogel
 */

#ifndef __H__UG__LIB_DISC__SPATIAL_DISC__ELEM_DISC__ELEM_DISC_ASSEMBLE_UTIL__
#define __H__UG__LIB_DISC__SPATIAL_DISC__ELEM_DISC__ELEM_DISC_ASSEMBLE_UTIL__

// extern includes
#include <iostream>
#include <vector>

// other ug4 modules
#include "common/common.h"

// intern headers
#include "../../reference_element/reference_element.h"
#include "./elem_disc_interface.h"
#include "lib_disc/common/function_group.h"
#include "lib_disc/common/local_algebra.h"
#include "lib_disc/spatial_disc/user_data/data_evaluator.h"
#include "lib_disc/spatial_disc/ass_adapter.h"

#define PROFILE_ELEM_LOOP
#ifdef PROFILE_ELEM_LOOP
	#define EL_PROFILE_FUNC()		PROFILE_FUNC()
	#define EL_PROFILE_BEGIN(name)	PROFILE_BEGIN(name)
	#define EL_PROFILE_END()		PROFILE_END()
#else
	#define EL_PROFILE_FUNC()
	#define EL_PROFILE_BEGIN(name)
	#define EL_PROFILE_END()
#endif


namespace ug {

////////////////////////////////////////////////////////////////////////////////
// Assemble Stiffness Matrix
////////////////////////////////////////////////////////////////////////////////

/**
 * This function adds to the Stiffness matrix the entries of one subset for all passed
 * element discretizations.
 *
 * \param[in]		vElemDisc		element discretizations
 * \param[in]		dd				DoF Distribution
 * \param[in]		iterBegin		element iterator
 * \param[in]		iterEnd			element iterator
 * \param[in]		si				subset index
 * \param[in]		bNonRegularGrid flag to indicate if non regular grid is used
 * \param[in,out]	A				Stiffness matrix
 * \param[in]		u				solution
 * \param[in]		assAdapt		assemble adapter
 */
template <typename TElem, typename TDD, typename TAlgebra, typename TIterator>
void
AssembleStiffnessMatrix(	const std::vector<IElemDisc*>& vElemDisc,
                        	ConstSmartPtr<TDD> dd,
        					TIterator iterBegin,
        					TIterator iterEnd,
                        	int si, bool bNonRegularGrid,
                        	typename TAlgebra::matrix_type& A,
                        	const typename TAlgebra::vector_type& u,
                        	AssAdapter& assAdapt)
{
// 	check if at least on element exist, else return
	if(iterBegin == iterEnd) return;

// 	get marker
	BoolMarker* mark = assAdapt.pBoolMarker;
// 	check if assemble routine should be called only with respect to one index
	size_t assInd;
	if (assAdapt.assIndex.index_set)
		assInd = assAdapt.assIndex.index;

	try
	{
	DataEvaluator Eval(STIFF,
	                   vElemDisc, dd->function_pattern(), si, bNonRegularGrid);

//	prepare element loop
	Eval.template prepare_elem_loop<TElem>();

// 	local indices and local algebra
	LocalIndices ind; LocalVector locU; LocalMatrix locA;

// 	Loop over all elements
	for(TIterator iter = iterBegin; iter != iterEnd; ++iter)
	{
	// 	get Element
		TElem* elem = *iter;

	//	check if elem is skipped from assembling
		if(mark) if(!mark->is_marked(elem)) continue;

	// 	get global indices
		dd->indices(elem, ind, Eval.use_hanging());

	// 	adapt local algebra
		locU.resize(ind); locA.resize(ind);

	// 	read local values of u
		GetLocalVector(locU, u);

	// 	prepare element
		try
		{
			Eval.prepare_elem(elem, locU, ind, true);
		}
		UG_CATCH_THROW("AssembleStiffnessMatrix Cannot prepare element.");

	// 	Assemble JA
		locA = 0.0;
		try
		{
			Eval.add_JA_elem(locA, locU, elem);
		}
		UG_CATCH_THROW("AssembleStiffnessMatrix: Cannot compute Jacobian (A).");

	// send local to global matrix
		if (assAdapt.assIndex.index_set){
			 /* the global matrix is only set up with respect
			  *	to one global index 'assInd' -> A is a (block,block)-matrix
			  *	at one index/DoF */
			AddLocalMatrixToGlobalAtIndex(A, locA, assInd);
		}
		else{
			AddLocalMatrixToGlobal(A, locA);
		}
	}

// 	finish element loop
	try
	{
		Eval.finish_elem_loop();
	}
	UG_CATCH_THROW("AssembleStiffnessMatrix: Cannot finish element loop.");

	}
	UG_CATCH_THROW("AssembleStiffnessMatrix': Cannot create Data Evaluator.");
}

template <typename TElem, typename TDD, typename TAlgebra>
void
AssembleStiffnessMatrix(	const std::vector<IElemDisc*>& vElemDisc,
                        	ConstSmartPtr<TDD> dd,
                        	int si, bool bNonRegularGrid,
                        	typename TAlgebra::matrix_type& A,
                        	const typename TAlgebra::vector_type& u,
                        	AssAdapter& assAdapt)
{
	//	check if only some elements are selected
	if(assAdapt.pSelector)
	{
		Selector* sel = assAdapt.pSelector;
		const ISubsetHandler& sh = *dd->subset_handler();

		std::vector<TElem*> elems;
		for(typename Selector::traits<TElem>::iterator iter = sel->begin<TElem>();
			iter != sel->end<TElem>(); ++iter)
		{
			if(sh.get_subset_index(*iter) == si)
				elems.push_back(*iter);
		}

		//	assembling is carried out only over those elements
		//	which are selected and in subset si
		AssembleStiffnessMatrix<TElem,TDD,TAlgebra>
			(vElemDisc, dd, elems.begin(), elems.end(), si,
			 bNonRegularGrid, A, u, assAdapt);
	}
	else
	{
		//	general case: assembling over all elements in subset si
		typedef typename TDD::template traits<TElem>::const_iterator const_iterator;

		const_iterator iterBegin = dd->template begin<TElem>(si);
		const_iterator iterEnd = dd->template end<TElem>(si);

		AssembleStiffnessMatrix<TElem, TDD, TAlgebra>
			(vElemDisc, dd, iterBegin, iterEnd, si,
					bNonRegularGrid, A, u, assAdapt);
	}
}

////////////////////////////////////////////////////////////////////////////////
// Assemble Mass Matrix
////////////////////////////////////////////////////////////////////////////////
/**
 * This function adds to the Mass matrix the entries of one subset for all passed
 * element discretizations.
 *
 * \param[in]		vElemDisc		element discretizations
 * \param[in]		dd				DoF Distribution
 * \param[in]		iterBegin		element iterator
 * \param[in]		iterEnd			element iterator
 * \param[in]		si				subset index
 * \param[in]		bNonRegularGrid flag to indicate if non regular grid is used
 * \param[in,out]	M				Mass matrix
 * \param[in]		u				solution
 * \param[in]		assAdapt		assemble adapter
 */
template <typename TElem, typename TDD, typename TAlgebra, typename TIterator>
void
AssembleMassMatrix(	const std::vector<IElemDisc*>& vElemDisc,
					ConstSmartPtr<TDD> dd,
					TIterator iterBegin,
					TIterator iterEnd,
					int si, bool bNonRegularGrid,
					typename TAlgebra::matrix_type& M,
					const typename TAlgebra::vector_type& u,
					AssAdapter& assAdapt)
{
// 	check if at least on element exist, else return
	if(iterBegin == iterEnd) return;

// 	get marker
	BoolMarker* mark = assAdapt.pBoolMarker;
// 	check if assemble routine should be called only with respect to one index
	size_t assInd;
	if (assAdapt.assIndex.index_set)
		assInd = assAdapt.assIndex.index;

//	prepare for given elem discs
	try
	{
	DataEvaluator Eval(MASS,
	                   vElemDisc, dd->function_pattern(), si, bNonRegularGrid);

//	prepare element loop
	Eval.template prepare_elem_loop<TElem>();

// 	local indices and local algebra
	LocalIndices ind; LocalVector locU; LocalMatrix locM;

// 	Loop over all elements
	for(TIterator iter = iterBegin; iter != iterEnd; ++iter)
	{
	// 	get Element
		TElem* elem = *iter;

	//	check if elem is skipped from assembling
		if(mark) if(!mark->is_marked(elem)) continue;

	// 	get global indices
		dd->indices(elem, ind, Eval.use_hanging());

	// 	adapt local algebra
		locU.resize(ind); locM.resize(ind);

	// 	read local values of u
		GetLocalVector(locU, u);

	// 	prepare element
		try
		{
			Eval.prepare_elem(elem, locU, ind, true);
		}
		UG_CATCH_THROW("AssembleMassMatrix: Cannot prepare element.");

	// 	Assemble JM
		locM = 0.0;
		try
		{
			Eval.add_JM_elem(locM, locU, elem);
		}
		UG_CATCH_THROW("AssembleMassMatrix: Cannot compute Jacobian (M).");

	// send local to global matrix
		if (assAdapt.assIndex.index_set){
			 /* the global matrix is only set up with respect
			  *	to one global index 'assInd' -> M is a (block,block)-matrix
			  *	at one index/DoF */
			AddLocalMatrixToGlobalAtIndex(M, locM, assInd);
		}
		else{
			AddLocalMatrixToGlobal(M, locM);
		}
	}

// 	finish element loop
	try
	{
		Eval.finish_elem_loop();
	}
	UG_CATCH_THROW("AssembleMassMatrix: Cannot finish element loop.");

	}
	UG_CATCH_THROW("AssembleMassMatrix: Cannot create Data Evaluator.");
}

template <typename TElem, typename TDD, typename TAlgebra>
void
AssembleMassMatrix(	const std::vector<IElemDisc*>& vElemDisc,
					ConstSmartPtr<TDD> dd,
					int si, bool bNonRegularGrid,
					typename TAlgebra::matrix_type& M,
					const typename TAlgebra::vector_type& u,
					AssAdapter& assAdapt)
{
	//	check if only some elements are selected
	if(assAdapt.pSelector)
	{
		Selector* sel = assAdapt.pSelector;
		const ISubsetHandler& sh = *dd->subset_handler();

		std::vector<TElem*> elems;
		for(typename Selector::traits<TElem>::iterator iter = sel->begin<TElem>();
			iter != sel->end<TElem>(); ++iter)
		{
			if(sh.get_subset_index(*iter) == si)
				elems.push_back(*iter);
		}

		//	assembling is carried out only over those elements
		//	which are selected and in subset si
		AssembleMassMatrix<TElem,TDD,TAlgebra>
			(vElemDisc, dd, elems.begin(), elems.end(), si,
			 bNonRegularGrid, M, u, assAdapt);
	}
	else
	{
		//	general case: assembling over all elements in subset si
		typedef typename TDD::template traits<TElem>::const_iterator const_iterator;

		const_iterator iterBegin = dd->template begin<TElem>(si);
		const_iterator iterEnd = dd->template end<TElem>(si);

		AssembleMassMatrix<TElem, TDD, TAlgebra>
			(vElemDisc, dd, iterBegin, iterEnd, si,
					bNonRegularGrid, M, u, assAdapt);
	}
}

////////////////////////////////////////////////////////////////////////////////
// Assemble (stationary) Jacobian
////////////////////////////////////////////////////////////////////////////////

/**
 * This function adds to the jacobian the entries of one subset for all passed
 * element discretizations.
 *
 * \param[in]		vElemDisc		element discretizations
 * \param[in]		dd				DoF Distribution
 * \param[in]		iterBegin		element iterator
 * \param[in]		iterEnd			element iterator
 * \param[in]		si				subset index
 * \param[in]		bNonRegularGrid flag to indicate if non regular grid is used
 * \param[in,out]	J				jacobian
 * \param[in]		u				solution
 * \param[in]		assAdapt		assemble adapter
 */
template <typename TElem, typename TDD, typename TAlgebra, typename TIterator>
void
AssembleJacobian(	const std::vector<IElemDisc*>& vElemDisc,
					ConstSmartPtr<TDD> dd,
					TIterator iterBegin,
					TIterator iterEnd,
					int si, bool bNonRegularGrid,
					typename TAlgebra::matrix_type& J,
					const typename TAlgebra::vector_type& u,
					AssAdapter& assAdapt)
{
// 	check if at least on element exist, else return
	if(iterBegin == iterEnd) return;

// 	get marker
	BoolMarker* mark = assAdapt.pBoolMarker;
// 	check if assemble routine should be called only with respect to one index
	size_t assInd;
	if (assAdapt.assIndex.index_set)
		assInd = assAdapt.assIndex.index;

//	prepare for given elem discs
	try
	{
	DataEvaluator Eval(STIFF | RHS,
	                   vElemDisc, dd->function_pattern(), si, bNonRegularGrid);

//	prepare element loop
	Eval.template prepare_elem_loop<TElem>();

// 	local indices and local algebra
	LocalIndices ind; LocalVector locU; LocalMatrix locJ;

// 	Loop over all elements
	for(TIterator iter = iterBegin; iter != iterEnd; ++iter)
	{
	// 	get Element
		TElem* elem = *iter;

	//	check if elem is skipped from assembling
		if(mark) if(!mark->is_marked(elem)) continue;

	// 	get global indices
		dd->indices(elem, ind, Eval.use_hanging());

	// 	adapt local algebra
		locU.resize(ind); locJ.resize(ind);

	// 	read local values of u
		GetLocalVector(locU, u);

	// 	prepare element
		try
		{
			Eval.prepare_elem(elem, locU, ind, true);
		}
		UG_CATCH_THROW("(stationary) AssembleJacobian: Cannot prepare element.");

	//	reset local algebra
		locJ = 0.0;

	// 	Assemble JA
		try
		{
			Eval.add_JA_elem(locJ, locU, elem);
		}
		UG_CATCH_THROW("(stationary) AssembleJacobian: Cannot compute Jacobian (A).");

	// send local to global matrix
		if (assAdapt.assIndex.index_set){
			 /* the global matrix is only set up with respect
			  *	to one global index 'assInd' -> J is a (block,block)-matrix
			  *	at one index/DoF */
			AddLocalMatrixToGlobalAtIndex(J, locJ, assInd);
		}
		else{
			AddLocalMatrixToGlobal(J, locJ);
		}
	}

// 	finish element loop
	try
	{
		Eval.finish_elem_loop();
	}
	UG_CATCH_THROW("(stationary) AssembleJacobian: Cannot finish element loop.");

	}
	UG_CATCH_THROW("(stationary) AssembleJacobian: Cannot create Data Evaluator.");
}

template <typename TElem, typename TDD, typename TAlgebra>
void
AssembleJacobian(	const std::vector<IElemDisc*>& vElemDisc,
					ConstSmartPtr<TDD> dd,
					int si, bool bNonRegularGrid,
					typename TAlgebra::matrix_type& J,
					const typename TAlgebra::vector_type& u,
					AssAdapter& assAdapt)
{
	//	check if only some elements are selected
	if(assAdapt.pSelector)
	{
		Selector* sel = assAdapt.pSelector;
		const ISubsetHandler& sh = *dd->subset_handler();

		std::vector<TElem*> elems;
		for(typename Selector::traits<TElem>::iterator iter = sel->begin<TElem>();
			iter != sel->end<TElem>(); ++iter)
		{
			if(sh.get_subset_index(*iter) == si)
				elems.push_back(*iter);
		}

		//	assembling is carried out only over those elements
		//	which are selected and in subset si
		AssembleJacobian<TElem,TDD,TAlgebra>
			(vElemDisc, dd, elems.begin(), elems.end(), si,
			 bNonRegularGrid, J, u, assAdapt);
	}
	else
	{
		//	general case: assembling over all elements in subset si
		typedef typename TDD::template traits<TElem>::const_iterator const_iterator;

		const_iterator iterBegin = dd->template begin<TElem>(si);
		const_iterator iterEnd = dd->template end<TElem>(si);

		AssembleJacobian<TElem, TDD, TAlgebra>
			(vElemDisc, dd, iterBegin, iterEnd, si,
					bNonRegularGrid, J, u, assAdapt);
	}
}

////////////////////////////////////////////////////////////////////////////////
// Assemble (instationary) Jacobian
////////////////////////////////////////////////////////////////////////////////

/**
 * This function adds to the jacobian the entries of one subset for all passed
 * element discretizations.
 * Note, that it is assumed	to have s_m0 == 1
 *
 * \param[in]		vElemDisc		element discretizations
 * \param[in]		dd				DoF Distribution
 * \param[in]		iterBegin		element iterator
 * \param[in]		iterEnd			element iterator
 * \param[in]		si				subset index
 * \param[in]		bNonRegularGrid flag to indicate if non regular grid is used
 * \param[in,out]	J				jacobian
 * \param[in]		vSol			current and previous solutions
 * \param[in]		s_a0			scaling factor for stiffness part
 * \param[in]		assAdapt		assemble adapter
 */
template <typename TElem, typename TDD, typename TAlgebra, typename TIterator>
void
AssembleJacobian(	const std::vector<IElemDisc*>& vElemDisc,
					ConstSmartPtr<TDD> dd,
					TIterator iterBegin,
					TIterator iterEnd,
					int si, bool bNonRegularGrid,
					typename TAlgebra::matrix_type& J,
					ConstSmartPtr<VectorTimeSeries<typename TAlgebra::vector_type> > vSol,
					number s_a0,
					AssAdapter& assAdapt)
{
// 	check if at least on element exist, else return
	if(iterBegin == iterEnd) return;

// 	get marker
	BoolMarker* mark = assAdapt.pBoolMarker;
// 	check if assemble routine should be called only with respect to one index
	size_t assInd;
	if (assAdapt.assIndex.index_set)
		assInd = assAdapt.assIndex.index;

//	get current time and vector
	const typename TAlgebra::vector_type& u = *vSol->solution(0);

//	create local time series
	LocalVectorTimeSeries locTimeSeries;
	locTimeSeries.read_times(vSol);

//	prepare for given elem discs
	try
	{
	DataEvaluator Eval(MASS | STIFF | RHS,
	                   vElemDisc, dd->function_pattern(), si, bNonRegularGrid,
					   &locTimeSeries);
	Eval.set_time_point(0);

//	prepare element loop
	Eval.template prepare_elem_loop<TElem>();

//	local algebra
	LocalIndices ind; LocalVector locU; LocalMatrix locJ;

	EL_PROFILE_BEGIN(Elem_AssembleJacobian);
// 	Loop over all elements
	for(TIterator iter = iterBegin; iter != iterEnd; ++iter)
	{
	// 	get Element
		TElem* elem = *iter;

	//	check if elem is skipped from assembling
		if(mark) if(!mark->is_marked(elem)) continue;

	// 	get global indices
		dd->indices(elem, ind, Eval.use_hanging());

	// 	adapt local algebra
		locU.resize(ind); locJ.resize(ind);

	// 	read local values of u
		GetLocalVector(locU, u);

	//	read local values of time series
		if(Eval.time_series_needed())
			locTimeSeries.read_values(vSol, ind);

	// 	prepare element
		try
		{
			Eval.prepare_elem(elem, locU, ind, true);
		}
		UG_CATCH_THROW("(instationary) AssembleJacobian: Cannot prepare element.");

	//	reset local algebra
		locJ = 0.0;

		EL_PROFILE_BEGIN(Elem_add_JA);
		// 	Assemble JA
		try
		{
			Eval.add_JA_elem(locJ, locU, elem, PT_INSTATIONARY);
			locJ *= s_a0;

			Eval.add_JA_elem(locJ, locU, elem, PT_STATIONARY);
		}
		UG_CATCH_THROW("(instationary) AssembleJacobian: Cannot compute Jacobian (A).");
		EL_PROFILE_END();

	// 	Assemble JM
		try
		{
			Eval.add_JM_elem(locJ, locU, elem, PT_INSTATIONARY);
		}
		UG_CATCH_THROW("(instationary) AssembleJacobian: Cannot compute Jacobian (M).");

	// send local to global matrix
		if (assAdapt.assIndex.index_set){
			 /* the global matrix is only set up with respect
			  *	to one global index 'assInd' -> J is a (block,block)-matrix
			  *	at one index/DoF */
			AddLocalMatrixToGlobalAtIndex(J, locJ, assInd);
		}
		else{
			AddLocalMatrixToGlobal(J, locJ);
		}

	}
	EL_PROFILE_END();

// 	finish element loop
	try
	{
		Eval.finish_elem_loop();
	}
	UG_CATCH_THROW("(instationary) AssembleJacobian: Cannot finish element loop.");

	}
	UG_CATCH_THROW("(instationary) AssembleJacobian: Cannot create Data Evaluator.");
}

template <typename TElem, typename TDD, typename TAlgebra>
void
AssembleJacobian(	const std::vector<IElemDisc*>& vElemDisc,
					ConstSmartPtr<TDD> dd,
					int si, bool bNonRegularGrid,
					typename TAlgebra::matrix_type& J,
					ConstSmartPtr<VectorTimeSeries<typename TAlgebra::vector_type> > vSol,
					number s_a0,
					AssAdapter& assAdapt)
{
	//	check if only some elements are selected
	if(assAdapt.pSelector)
	{
		Selector* sel = assAdapt.pSelector;
		const ISubsetHandler& sh = *dd->subset_handler();

		std::vector<TElem*> elems;
		for(typename Selector::traits<TElem>::iterator iter = sel->begin<TElem>();
			iter != sel->end<TElem>(); ++iter)
		{
			if(sh.get_subset_index(*iter) == si)
				elems.push_back(*iter);
		}
		//UG_LOG(elems.size() << "Elemente in Subset" << si << "ausgew�hlt (assJac) \n");

		//	assembling is carried out only over those elements
		//	which are selected and in subset si
		AssembleJacobian<TElem,TDD,TAlgebra>
			(vElemDisc, dd, elems.begin(), elems.end(), si,
			 bNonRegularGrid, J, vSol, s_a0, assAdapt);
	}
	else
	{
		//	general case: assembling over all elements in subset si
		typedef typename TDD::template traits<TElem>::const_iterator const_iterator;

		const_iterator iterBegin = dd->template begin<TElem>(si);
		const_iterator iterEnd = dd->template end<TElem>(si);

		AssembleJacobian<TElem, TDD, TAlgebra>
			(vElemDisc, dd, iterBegin, iterEnd, si,
					bNonRegularGrid, J, vSol, s_a0, assAdapt);
	}
}

////////////////////////////////////////////////////////////////////////////////
// Assemble (stationary) Defect
////////////////////////////////////////////////////////////////////////////////

/**
 * This function adds to the defect the entries of one subset for all passed
 * element discretizations.
 *
 * \param[in]		vElemDisc		element discretizations
 * \param[in]		dd				DoF Distribution
 * \param[in]		iterBegin		element iterator
 * \param[in]		iterEnd			element iterator
 * \param[in]		si				subset index
 * \param[in]		bNonRegularGrid flag to indicate if non regular grid is used
 * \param[in,out]	d				defect
 * \param[in]		u				solution
 * \param[in]		assAdapt		assemble adapter
 */
template <typename TElem, typename TDD, typename TAlgebra, typename TIterator>
void
AssembleDefect(	const std::vector<IElemDisc*>& vElemDisc,
               	ConstSmartPtr<TDD> dd,
				TIterator iterBegin,
				TIterator iterEnd,
               	int si, bool bNonRegularGrid,
               	typename TAlgebra::vector_type& d,
               	const typename TAlgebra::vector_type& u,
               	AssAdapter& assAdapt)
{
// 	check if at least on element exist, else return
	if(iterBegin == iterEnd) return;

// 	get marker
	BoolMarker* mark = assAdapt.pBoolMarker;
// 	check if assemble routine should be called only with respect to one index
	size_t assInd;
	if (assAdapt.assIndex.index_set)
		assInd = assAdapt.assIndex.index;

//	prepare for given elem discs
	try
	{
	DataEvaluator Eval(STIFF | RHS,
	                   vElemDisc, dd->function_pattern(), si, bNonRegularGrid);

//	prepare element loop
	Eval.template prepare_elem_loop<TElem>();

// 	local indices and local algebra
	LocalIndices ind; LocalVector locU, locD, tmpLocD;

// 	Loop over all elements
	for(TIterator iter = iterBegin; iter != iterEnd; ++iter)
	{
	// 	get Element
		TElem* elem = *iter;

	//	check if elem is skipped from assembling
		if(mark) if(!mark->is_marked(elem)) continue;

	// 	get global indices
		dd->indices(elem, ind, Eval.use_hanging());

	// 	adapt local algebra
		locU.resize(ind); locD.resize(ind); tmpLocD.resize(ind);

	// 	read local values of u
		GetLocalVector(locU, u);

	// 	prepare element
		try
		{
			Eval.prepare_elem(elem, locU, ind);
		}
		UG_CATCH_THROW("(stationary) AssembleDefect: Cannot prepare element.");

	//	reset local algebra
		locD = 0.0;

	// 	Assemble A
		try
		{
			Eval.add_dA_elem(locD, locU, elem);
		}
		UG_CATCH_THROW("(stationary) AssembleDefect: Cannot compute Defect (A).");

	// 	Assemble rhs
		try
		{
			tmpLocD = 0.0;
			Eval.add_rhs_elem(tmpLocD, elem);
			locD.scale_append(-1, tmpLocD);
		}
		UG_CATCH_THROW("(stationary) AssembleDefect: Cannot compute Rhs.");

	// 	send local to global rhs
		if (assAdapt.assIndex.index_set){
			 /* the global vector is only set up with respect
			  *	to one global index 'assInd' -> d is a (block)-vector
			  *	at one index/DoF */
			AddLocalVectorAtIndex(d, locD, assInd);
		}
		else{
			AddLocalVector(d, locD);
		}
	}

// 	finish element loop
	try
	{
		Eval.finish_elem_loop();
	}
	UG_CATCH_THROW("(stationary) AssembleDefect: Cannot finish element loop.");

	}
	UG_CATCH_THROW("(stationary) AssembleDefect: Cannot create Data Evaluator.");
}

template <typename TElem, typename TDD, typename TAlgebra>
void
AssembleDefect(	const std::vector<IElemDisc*>& vElemDisc,
               	ConstSmartPtr<TDD> dd,
               	int si, bool bNonRegularGrid,
               	typename TAlgebra::vector_type& d,
               	const typename TAlgebra::vector_type& u,
               	AssAdapter& assAdapt)
{
	//	check if only some elements are selected
	if(assAdapt.pSelector)
	{
		Selector* sel = assAdapt.pSelector;
		const ISubsetHandler& sh = *dd->subset_handler();

		std::vector<TElem*> elems;
		for(typename Selector::traits<TElem>::iterator iter = sel->begin<TElem>();
			iter != sel->end<TElem>(); ++iter)
		{
			if(sh.get_subset_index(*iter) == si)
				elems.push_back(*iter);
		}

		//	assembling is carried out only over those elements
		//	which are selected and in subset si
		AssembleDefect<TElem,TDD,TAlgebra>
			(vElemDisc, dd, elems.begin(), elems.end(), si,
			 bNonRegularGrid, d, u, assAdapt);
	}
	else
	{
		//	general case: assembling over all elements in subset si
		typedef typename TDD::template traits<TElem>::const_iterator const_iterator;

		const_iterator iterBegin = dd->template begin<TElem>(si);
		const_iterator iterEnd = dd->template end<TElem>(si);

		AssembleDefect<TElem, TDD, TAlgebra>
			(vElemDisc, dd, iterBegin, iterEnd, si,
					bNonRegularGrid, d, u, assAdapt);
	}
}

////////////////////////////////////////////////////////////////////////////////
// Assemble (instationary) Defect
////////////////////////////////////////////////////////////////////////////////

/**
 * This function adds to the defect the entries of one subset for all passed
 * element discretizations.
 *
 * \param[in]		vElemDisc		element discretizations
 * \param[in]		dd				DoF Distribution
 * \param[in]		iterBegin		element iterator
 * \param[in]		iterEnd			element iterator
 * \param[in]		si				subset index
 * \param[in]		bNonRegularGrid flag to indicate if non regular grid is used
 * \param[in,out]	d				defect
 * \param[in]		vSol			current and previous solutions
 * \param[in]		vScaleMass		scaling factors for mass part
 * \param[in]		vScaleStiff		scaling factors for stiffness part
 * \param[in]		assAdapt		assemble adapter
 */
template <typename TElem, typename TDD, typename TAlgebra, typename TIterator>
void
AssembleDefect(	const std::vector<IElemDisc*>& vElemDisc,
               	ConstSmartPtr<TDD> dd,
				TIterator iterBegin,
				TIterator iterEnd,
               	int si, bool bNonRegularGrid,
               	typename TAlgebra::vector_type& d,
                ConstSmartPtr<VectorTimeSeries<typename TAlgebra::vector_type> > vSol,
				const std::vector<number>& vScaleMass,
				const std::vector<number>& vScaleStiff,
				AssAdapter& assAdapt)
{
// 	check if at least on element exist, else return
	if(iterBegin == iterEnd) return;

// 	get marker
	BoolMarker* mark = assAdapt.pBoolMarker;
// 	check if assemble routine should be called only with respect to one index
	size_t assInd;
	if (assAdapt.assIndex.index_set)
		assInd = assAdapt.assIndex.index;

//	check time scheme
	if(vScaleMass.size() != vScaleStiff.size())
		UG_THROW("(instationary) AssembleDefect: s_a and s_m must have same size.");

	if(vSol->size() < vScaleStiff.size())
		UG_THROW("(instationary) AssembleDefect: Time stepping scheme needs at "
				"least "<<vScaleStiff.size()<<" time steps, but only "<<
				vSol->size() << " passed.");

//	create local time series
	LocalVectorTimeSeries locTimeSeries;
	locTimeSeries.read_times(vSol);

//	prepare for given elem discs
	try
	{
	DataEvaluator Eval(MASS | STIFF | RHS,
	                   vElemDisc, dd->function_pattern(), si, bNonRegularGrid,
	                   &locTimeSeries, &vScaleMass, &vScaleStiff);

//	prepare element loop
	Eval.template prepare_elem_loop<TElem>();

// 	local indices and local algebra
	LocalIndices ind; LocalVector locD, tmpLocD;

// 	Loop over all elements
	for(TIterator iter = iterBegin; iter != iterEnd; ++iter)
	{
	// 	get Element
		TElem* elem = *iter;

	//	check if elem is skipped from assembling
		if(mark) if(!mark->is_marked(elem)) continue;

	// 	get global indices
		dd->indices(elem, ind, Eval.use_hanging());

	// 	adapt local algebra
		locD.resize(ind); tmpLocD.resize(ind);

	//	read local values of time series
		locTimeSeries.read_values(vSol, ind);

	//	reset contribution of this element
		locD = 0.0;

	//	loop all time points and assemble them
		for(size_t t = 0; t < vScaleStiff.size(); ++t)
		{
		//	get local solution at timepoint
			LocalVector& locU = locTimeSeries.solution(t);
			Eval.set_time_point(t);

		// 	prepare element
			try
			{
				Eval.prepare_elem(elem, locU, ind, false);
			}
			UG_CATCH_THROW("(instationary) AssembleDefect: Cannot prepare element.");

		// 	Assemble M
			try
			{
				tmpLocD = 0.0;
				Eval.add_dM_elem(tmpLocD, locU, elem, PT_INSTATIONARY);
				locD.scale_append(vScaleMass[t], tmpLocD);
			}
			UG_CATCH_THROW("(instationary) AssembleDefect: Cannot compute Defect (M).");

		// 	Assemble A
			try
			{
				tmpLocD = 0.0;
				Eval.add_dA_elem(tmpLocD, locU, elem, PT_INSTATIONARY);
				locD.scale_append(vScaleStiff[t], tmpLocD);

				if(t == 0)
					Eval.add_dA_elem(locD, locU, elem, PT_STATIONARY);
			}
			UG_CATCH_THROW("(instationary) AssembleDefect: Cannot compute Defect (A).");

			// NEW: explicit reaction
			// only point where implementation
			// has to be improved for higher time orders
			// in time-solver!!!


			// vScaleStiff[t] sind die s_a  d.h. wenn wir die 
			// vScaleStiff[0] zu s_a = tau haben dann  
			// haben wir die Zeitschrittweite
			// wenn wir in niedrigster t-Ordnung eine Null haben
			// dann sind die s_{a,1} null 
			// und dann muessen die expliziten Reaktionsterme 
			// kuenstlich 
			// hinzugefuegt werden, 
			// Frage woher bekommt man tau wenn sa = 0 ist?????
			// man muss vermutlich fuer t = 1 dann s_a,0 abfragen 
 
			// start new central function		       


			if( t == 1 ) // only valid at lowest timediscretization order
			{
          		   // 	   Assemble A expliziter Anteil
			   tmpLocD = 0.0;
			   try
			   {
			     Eval.add_dA_elem_explicit(tmpLocD, locU, elem, PT_INSTATIONARY);
			   }
			   UG_CATCH_THROW("(instationary) AssembleDefect explizit: Cannot compute Defect (A).");

			   //UG_ASSERT(vScaleStiff.size() == 1, "Only one step method supoorted.");
			   const number dt = vSol->time(0)-vSol->time(1);
			   locD.scale_append(dt, tmpLocD);
			}

			// end new central function which has to be improved

		// 	Assemble rhs
			try
			{
				tmpLocD = 0.0;
				Eval.add_rhs_elem(tmpLocD, elem, PT_INSTATIONARY);
				locD.scale_append( -vScaleStiff[t], tmpLocD);

				if(t==0){
					tmpLocD = 0.0;
					Eval.add_rhs_elem(tmpLocD, elem, PT_STATIONARY);
					locD.scale_append( -1.0, tmpLocD);
				}
			}
			UG_CATCH_THROW("(instationary) AssembleDefect: Cannot compute Rhs.");
		}

	// 	send local to global rhs
		if (assAdapt.assIndex.index_set){
			 /* the global vector is only set up with respect
			  *	to one global index 'assInd' -> d is a (block)-vector
			  *	at one index/DoF */
			AddLocalVectorAtIndex(d, locD, assInd);
		}
		else{
			AddLocalVector(d, locD);
		}
	}

// 	finish element loop
	try
	{
		Eval.finish_elem_loop();
	}
	UG_CATCH_THROW("(instationary) AssembleDefect: Cannot finish element loop.");

	}
	UG_CATCH_THROW("(instationary) AssembleDefect: Cannot create Data Evaluator.");
}

template <typename TElem, typename TDD, typename TAlgebra>
void
AssembleDefect(	const std::vector<IElemDisc*>& vElemDisc,
               	ConstSmartPtr<TDD> dd,
               	int si, bool bNonRegularGrid,
               	typename TAlgebra::vector_type& d,
                ConstSmartPtr<VectorTimeSeries<typename TAlgebra::vector_type> > vSol,
				const std::vector<number>& vScaleMass,
				const std::vector<number>& vScaleStiff,
				AssAdapter& assAdapt)
{
	//	check if only some elements are selected
	if(assAdapt.pSelector)
	{
		Selector* sel = assAdapt.pSelector;
		const ISubsetHandler& sh = *dd->subset_handler();

		std::vector<TElem*> elems;
		for(typename Selector::traits<TElem>::iterator iter = sel->begin<TElem>();
			iter != sel->end<TElem>(); ++iter)
		{
			if(sh.get_subset_index(*iter) == si)
				elems.push_back(*iter);
		}
		//UG_LOG(elems.size() << "Elemente in Subset" << si << "ausgew�hlt (assDef) \n");

		//	assembling is carried out only over those elements
		//	which are selected and in subset si
		AssembleDefect<TElem,TDD,TAlgebra>
			(vElemDisc, dd, elems.begin(), elems.end(), si,
			 bNonRegularGrid, d, vSol, vScaleMass, vScaleStiff, assAdapt);
	}
	else
	{
		//	general case: assembling over all elements in subset si
		typedef typename TDD::template traits<TElem>::const_iterator const_iterator;

		const_iterator iterBegin = dd->template begin<TElem>(si);
		const_iterator iterEnd = dd->template end<TElem>(si);

		AssembleDefect<TElem, TDD, TAlgebra>
			(vElemDisc, dd, iterBegin, iterEnd, si,
					bNonRegularGrid, d, vSol, vScaleMass, vScaleStiff, assAdapt);
	}
}

////////////////////////////////////////////////////////////////////////////////
// Assemble (stationary) Linear
////////////////////////////////////////////////////////////////////////////////

/**
 * This function adds to the Matrix and to the Rhs the entries of one subset
 * for all passed element discretizations.
 *
 * \param[in]		vElemDisc		element discretizations
 * \param[in]		dd				DoF Distribution
 * \param[in]		iterBegin		element iterator
 * \param[in]		iterEnd			element iterator
 * \param[in]		si				subset index
 * \param[in]		bNonRegularGrid flag to indicate if non regular grid is used
 * \param[in,out]	A				Matrix
 * \param[in,out]	rhs				Right-hand side
 * \param[in]		assAdapt		assemble adapter
 */
template <typename TElem, typename TDD, typename TAlgebra, typename TIterator>
void
AssembleLinear(	const std::vector<IElemDisc*>& vElemDisc,
               	ConstSmartPtr<TDD> dd,
				TIterator iterBegin,
				TIterator iterEnd,
               	int si, bool bNonRegularGrid,
               	typename TAlgebra::matrix_type& A,
               	typename TAlgebra::vector_type& rhs,
               	AssAdapter& assAdapt)
{
// 	check if at least on element exist, else return
	if(iterBegin == iterEnd) return;

// 	get marker
	BoolMarker* mark = assAdapt.pBoolMarker;
// 	check if assemble routine should be called only with respect to one index
	size_t assInd;
	if (assAdapt.assIndex.index_set)
		assInd = assAdapt.assIndex.index;

//	prepare for given elem discs
	try
	{
	DataEvaluator Eval(STIFF | RHS,
	                   vElemDisc, dd->function_pattern(), si, bNonRegularGrid);

//	prepare loop
	Eval.template prepare_elem_loop<TElem>();

// 	local indices and local algebra
	LocalIndices ind; LocalVector locRhs; LocalMatrix locA;

// 	Loop over all elements
	for(TIterator iter = iterBegin; iter != iterEnd; ++iter)
	{
	// 	get Element
		TElem* elem = *iter;

	//	check if elem is skipped from assembling
		if(mark) if(!mark->is_marked(elem)) continue;

	// 	get global indices
		dd->indices(elem, ind, Eval.use_hanging());

	// 	adapt local algebra
		locRhs.resize(ind); locA.resize(ind);

	// 	prepare element
		try
		{
			Eval.prepare_elem(elem, locRhs, ind);
		}
		UG_CATCH_THROW("(stationary) AssembleLinear: Cannot prepare element.");

	//	reset local algebra
		locA = 0.0;
		locRhs = 0.0;

	// 	Assemble JA
		try
		{
			Eval.add_JA_elem(locA, locRhs, elem);
		}
		UG_CATCH_THROW("(stationary) AssembleLinear: Cannot compute Jacobian (A).");

	// 	Assemble rhs
		try
		{
			Eval.add_rhs_elem(locRhs, elem);
		}
		UG_CATCH_THROW("(stationary) AssembleLinear: Cannot compute Rhs.");

	// 	send local to global matrix & rhs
		if (assAdapt.assIndex.index_set){
			 /* the global vector/matrix is only set up with respect
			  *	to one global index 'assInd' -> rhs/A is a (block)-vector/(block,block)-matrix
			  *	at one index/DoF */
			AddLocalMatrixToGlobalAtIndex(A, locA, assInd);
			AddLocalVectorAtIndex(rhs, locRhs, assInd);
		}
		else{
			AddLocalMatrixToGlobal(A, locA);
			AddLocalVector(rhs, locRhs);
		}
	}

// 	finish element loop
	try
	{
		Eval.finish_elem_loop();
	}
	UG_CATCH_THROW("(stationary) AssembleLinear: Cannot finish element loop.");

	}
	UG_CATCH_THROW("(stationary) AssembleLinear: Cannot create Data Evaluator.");
}

template <typename TElem, typename TDD, typename TAlgebra>
void
AssembleLinear(	const std::vector<IElemDisc*>& vElemDisc,
               	ConstSmartPtr<TDD> dd,
               	int si, bool bNonRegularGrid,
               	typename TAlgebra::matrix_type& A,
               	typename TAlgebra::vector_type& rhs,
               	AssAdapter& assAdapt)
{
	//	check if only some elements are selected
	if(assAdapt.pSelector)
	{
		Selector* sel = assAdapt.pSelector;
		const ISubsetHandler& sh = *dd->subset_handler();

		std::vector<TElem*> elems;
		for(typename Selector::traits<TElem>::iterator iter = sel->begin<TElem>();
			iter != sel->end<TElem>(); ++iter)
		{
			if(sh.get_subset_index(*iter) == si)
				elems.push_back(*iter);
		}

		//	assembling is carried out only over those elements
		//	which are selected and in subset si
		AssembleLinear<TElem,TDD,TAlgebra>
			(vElemDisc, dd, elems.begin(), elems.end(), si,
			 bNonRegularGrid, A, rhs, assAdapt);
	}
	else
	{
		//	general case: assembling over all elements in subset si
		typedef typename TDD::template traits<TElem>::const_iterator const_iterator;

		const_iterator iterBegin = dd->template begin<TElem>(si);
		const_iterator iterEnd = dd->template end<TElem>(si);

		AssembleLinear<TElem, TDD, TAlgebra>
			(vElemDisc, dd, iterBegin, iterEnd, si,
					bNonRegularGrid, A, rhs, assAdapt);
	}
}

////////////////////////////////////////////////////////////////////////////////
// Assemble (instationary) Linear
////////////////////////////////////////////////////////////////////////////////

/**
 * This function adds to the Matrix and to the Rhs the entries of one subset
 * for all passed element discretizations.
 *
 * \param[in]		vElemDisc		element discretizations
 * \param[in]		dd				DoF Distribution
 * \param[in]		iterBegin		element iterator
 * \param[in]		iterEnd			element iterator
 * \param[in]		si				subset index
 * \param[in]		bNonRegularGrid flag to indicate if non regular grid is used
 * \param[in,out]	A				Matrix
 * \param[in,out]	rhs				Right-hand side
 * \param[in]		vSol			current and previous solutions
 * \param[in]		vScaleMass		scaling factors for mass part
 * \param[in]		vScaleStiff		scaling factors for stiffness part
 * \param[in]		mark			BoolMarker
 */
template <typename TElem, typename TDD, typename TAlgebra, typename TIterator>
void
AssembleLinear(	const std::vector<IElemDisc*>& vElemDisc,
               	ConstSmartPtr<TDD> dd,
				TIterator iterBegin,
				TIterator iterEnd,
               	int si, bool bNonRegularGrid,
               	typename TAlgebra::matrix_type& A,
               	typename TAlgebra::vector_type& rhs,
                ConstSmartPtr<VectorTimeSeries<typename TAlgebra::vector_type> > vSol,
               	const std::vector<number>& vScaleMass,
               	const std::vector<number>& vScaleStiff,
               	AssAdapter& assAdapt)
{
// 	check if at least on element exist, else return
	if(iterBegin == iterEnd) return;

// 	get marker
	BoolMarker* mark = assAdapt.pBoolMarker;
// 	check if assemble routine should be called only with respect to one index
	size_t assInd;
	if (assAdapt.assIndex.index_set)
		assInd = assAdapt.assIndex.index;

//	check time scheme
	if(vScaleMass.size() != vScaleStiff.size())
		UG_THROW("(instationary) AssembleLinear: s_a and s_m must have same size.");

	if(vSol->size() < vScaleStiff.size())
		UG_THROW("(instationary) AssembleLinear: Time stepping scheme needs at "
				"least "<<vScaleStiff.size()<<" time steps, but only "<<
				vSol->size() << " passed.");

//	create local time solution
	LocalVectorTimeSeries locTimeSeries;
	locTimeSeries.read_times(vSol);

//	prepare for given elem discs
	try
	{
	DataEvaluator Eval(MASS | STIFF | RHS,
	                   vElemDisc, dd->function_pattern(), si, bNonRegularGrid,
					   &locTimeSeries, &vScaleMass, &vScaleStiff);

//	prepare loop
	Eval.template prepare_elem_loop<TElem>();

//	local algebra
	LocalIndices ind; LocalVector locRhs, tmpLocRhs; LocalMatrix locA, tmpLocA;

// 	Loop over all elements
	for(TIterator iter = iterBegin; iter != iterEnd; ++iter)
	{
	// 	get Element
		TElem* elem = *iter;

	//	check if elem is skipped from assembling
		if(mark) if(!mark->is_marked(elem)) continue;

	// 	get global indices
		dd->indices(elem, ind, Eval.use_hanging());

	// 	adapt local algebra
		locRhs.resize(ind); tmpLocRhs.resize(ind);
		locA.resize(ind); tmpLocA.resize(ind);

	//	read local values of time series
		locTimeSeries.read_values(vSol, ind);
		Eval.set_time_point(0);

	//	reset element contribution
		locA = 0.0; locRhs = 0.0;

	/////////////////////
	//	current time step

	//	get local solution at time point
		LocalVector& locU = locTimeSeries.solution(0);

	// 	prepare element
		try
		{
			Eval.prepare_elem(elem, locU, ind, false);
		}
		UG_CATCH_THROW("(instationary) AssembleLinear: Cannot prepare element.");

	// 	Assemble JM
		try
		{
			tmpLocA = 0.0;
			Eval.add_JM_elem(tmpLocA, locU, elem, PT_INSTATIONARY);
			locA.scale_append(vScaleMass[0], tmpLocA);
		}
		UG_CATCH_THROW("(instationary) AssembleLinear: Cannot compute Jacobian (M).");

	// 	Assemble JA
		try
		{
			tmpLocA = 0.0;
			Eval.add_JA_elem(tmpLocA, locU, elem, PT_INSTATIONARY);
			locA.scale_append(vScaleStiff[0], tmpLocA);

			Eval.add_JA_elem(locA, locU, elem, PT_STATIONARY);
		}
		UG_CATCH_THROW("(instationary) AssembleLinear: Cannot compute Jacobian (A).");

	// 	Assemble rhs
		try
		{
			tmpLocRhs = 0.0;
			Eval.add_rhs_elem(tmpLocRhs, elem, PT_INSTATIONARY);
			locRhs.scale_append(vScaleStiff[0], tmpLocRhs);

			Eval.add_rhs_elem(locRhs, elem, PT_STATIONARY);
		}
		UG_CATCH_THROW("(instationary) AssembleLinear: Cannot compute Rhs.");

	///////////////////
	//	old time steps

	//	loop all old time points
		for(size_t t = 1; t < vScaleStiff.size(); ++t)
		{
		//	get local solution at time point
			LocalVector& locU = locTimeSeries.solution(t);
			Eval.set_time_point(t);

		// 	prepare element
			try
			{
				Eval.prepare_elem(elem, locU, ind, false);
			}
			UG_CATCH_THROW("(instationary) AssembleLinear: Cannot prepare element.");

		// 	Assemble dM
			try
			{
				tmpLocRhs = 0.0;
				Eval.add_dM_elem(tmpLocRhs, locU, elem, PT_INSTATIONARY);
				locRhs.scale_append(-vScaleMass[t], tmpLocRhs);
			}
			UG_CATCH_THROW("(instationary) AssembleLinear: Cannot compute Jacobian (M).");

		// 	Assemble dA
			try
			{
				tmpLocRhs = 0.0;
				Eval.add_dA_elem(tmpLocRhs, locU, elem, PT_INSTATIONARY);
				locRhs.scale_append(-vScaleStiff[t], tmpLocRhs);
			}
			UG_CATCH_THROW("(instationary) AssembleLinear: Cannot compute Jacobian (A).");

		// 	Assemble rhs
			try
			{
				tmpLocRhs = 0.0;
				Eval.add_rhs_elem(tmpLocRhs, elem, PT_INSTATIONARY);
				locRhs.scale_append(vScaleStiff[t], tmpLocRhs);
			}
			UG_CATCH_THROW("(instationary) AssembleLinear: Cannot compute Rhs.");
		}

		// 	send local to global matrix & rhs
			if (assAdapt.assIndex.index_set){
				 /* the global vector/matrix is only set up with respect
				  *	to one global index 'assInd' -> rhs/A is a (block)-vector/(block,block)-matrix
				  *	at one index/DoF */
				AddLocalMatrixToGlobalAtIndex(A, locA, assInd);
				AddLocalVectorAtIndex(rhs, locRhs, assInd);
			}
			else{
				AddLocalMatrixToGlobal(A, locA);
				AddLocalVector(rhs, locRhs);
			}
	}

// 	finish element loop
	try
	{
		Eval.finish_elem_loop();
	}
	UG_CATCH_THROW("(instationary) AssembleLinear: Cannot finish element loop.");

	}
	UG_CATCH_THROW("(instationary) AssembleLinear: Cannot create Data Evaluator.");
}

template <typename TElem, typename TDD, typename TAlgebra>
void
AssembleLinear(	const std::vector<IElemDisc*>& vElemDisc,
               	ConstSmartPtr<TDD> dd,
               	int si, bool bNonRegularGrid,
               	typename TAlgebra::matrix_type& A,
               	typename TAlgebra::vector_type& rhs,
                ConstSmartPtr<VectorTimeSeries<typename TAlgebra::vector_type> > vSol,
               	const std::vector<number>& vScaleMass,
               	const std::vector<number>& vScaleStiff,
               	AssAdapter& assAdapt)
{
	//	check if only some elements are selected
	if(assAdapt.pSelector)
	{
		Selector* sel = assAdapt.pSelector;
		const ISubsetHandler& sh = *dd->subset_handler();

		std::vector<TElem*> elems;
		for(typename Selector::traits<TElem>::iterator iter = sel->begin<TElem>();
			iter != sel->end<TElem>(); ++iter)
		{
			if(sh.get_subset_index(*iter) == si)
				elems.push_back(*iter);
		}

		//	assembling is carried out only over those elements
		//	which are selected and in subset si
		AssembleLinear<TElem,TDD,TAlgebra>
			(vElemDisc, dd, elems.begin(), elems.end(), si,
			 bNonRegularGrid, A, rhs, vSol, vScaleMass, vScaleStiff, assAdapt);
	}
	else
	{
		//	general case: assembling over all elements in subset si
		typedef typename TDD::template traits<TElem>::const_iterator const_iterator;

		const_iterator iterBegin = dd->template begin<TElem>(si);
		const_iterator iterEnd = dd->template end<TElem>(si);

		AssembleLinear<TElem, TDD, TAlgebra>
			(vElemDisc, dd, iterBegin, iterEnd, si,
					bNonRegularGrid, A, rhs, vSol, vScaleMass, vScaleStiff, assAdapt);
	}
}

////////////////////////////////////////////////////////////////////////////////
// Assemble Rhs
////////////////////////////////////////////////////////////////////////////////

/**
 * This function adds to the Rhs the entries of one subset
 * for all passed element discretizations.
 *
 * \param[in]		vElemDisc		element discretizations
 * \param[in]		dd				DoF Distribution
 * \param[in]		iterBegin		element iterator
 * \param[in]		iterEnd			element iterator
 * \param[in]		si				subset index
 * \param[in]		bNonRegularGrid flag to indicate if non regular grid is used
 * \param[in,out]	rhs				Right-hand side
 * \param[in]		u				solution
 * \param[in]		assAdapt		assemble adapter
 */
template <typename TElem, typename TDD, typename TAlgebra, typename TIterator>
void
AssembleRhs(	const std::vector<IElemDisc*>& vElemDisc,
               	ConstSmartPtr<TDD> dd,
				TIterator iterBegin,
				TIterator iterEnd,
               	int si, bool bNonRegularGrid,
               	typename TAlgebra::vector_type& rhs,
               	const typename TAlgebra::vector_type& u,
               	AssAdapter& assAdapt)
{
// 	check if at least on element exist, else return
	if(iterBegin == iterEnd) return;

// 	get marker
	BoolMarker* mark = assAdapt.pBoolMarker;
// 	check if assemble routine should be called only with respect to one index
	size_t assInd;
	if (assAdapt.assIndex.index_set)
		assInd = assAdapt.assIndex.index;

//	prepare for given elem discs
	try
	{
	DataEvaluator Eval(RHS,
	                   vElemDisc, dd->function_pattern(), si, bNonRegularGrid);

//	prepare loop
	Eval.template prepare_elem_loop<TElem>();

// 	local indices and local algebra
	LocalIndices ind; LocalVector locU, locRhs;

// 	Loop over all elements
	for(TIterator iter = iterBegin; iter != iterEnd; ++iter)
	{
	// 	get Element
		TElem* elem = *iter;

	//	check if elem is skipped from assembling
		if(mark) if(!mark->is_marked(elem)) continue;

	// 	get global indices
		dd->indices(elem, ind, Eval.use_hanging());

	// 	adapt local algebra
		locU.resize(ind); locRhs.resize(ind);

	// 	read local values of u
		GetLocalVector(locU, u);

	// 	prepare element
		try
		{
			Eval.prepare_elem(elem, locU, ind);
		}
		UG_CATCH_THROW("AssembleRhs: Cannot prepare element.");

	//	reset local algebra
		locRhs = 0.0;

	// 	Assemble rhs
		try
		{
			Eval.add_rhs_elem(locRhs, elem);
		}
		UG_CATCH_THROW("AssembleRhs: Cannot compute Rhs.");

	// 	send local to global rhs
		if (assAdapt.assIndex.index_set){
			 /* the global vector is only set up with respect
			  *	to one global index 'assInd' -> rhs is a (block)-vector
			  *	at one index/DoF */
			AddLocalVectorAtIndex(rhs, locRhs, assInd);
		}
		else{
			AddLocalVector(rhs, locRhs);
		}
	}

// 	finish element loop
	try
	{
		Eval.finish_elem_loop();
	}
	UG_CATCH_THROW("AssembleRhs: Cannot finish element loop.");

	}
	UG_CATCH_THROW("AssembleRhs: Cannot create Data Evaluator.");
}

template <typename TElem, typename TDD, typename TAlgebra>
void
AssembleRhs(	const std::vector<IElemDisc*>& vElemDisc,
               	ConstSmartPtr<TDD> dd,
               	int si, bool bNonRegularGrid,
               	typename TAlgebra::vector_type& rhs,
               	const typename TAlgebra::vector_type& u,
               	AssAdapter& assAdapt)
{
	//	check if only some elements are selected
	if(assAdapt.pSelector)
	{
		Selector* sel = assAdapt.pSelector;
		const ISubsetHandler& sh = *dd->subset_handler();

		std::vector<TElem*> elems;
		for(typename Selector::traits<TElem>::iterator iter = sel->begin<TElem>();
			iter != sel->end<TElem>(); ++iter)
		{
			if(sh.get_subset_index(*iter) == si)
				elems.push_back(*iter);
		}

		//	assembling is carried out only over those elements
		//	which are selected and in subset si
		AssembleRhs<TElem,TDD,TAlgebra>
			(vElemDisc, dd, elems.begin(), elems.end(), si,
			 bNonRegularGrid, rhs, u, assAdapt);
	}
	else
	{
		//	general case: assembling over all elements in subset si
		typedef typename TDD::template traits<TElem>::const_iterator const_iterator;

		const_iterator iterBegin = dd->template begin<TElem>(si);
		const_iterator iterEnd = dd->template end<TElem>(si);

		AssembleRhs<TElem, TDD, TAlgebra>
			(vElemDisc, dd, iterBegin, iterEnd, si,
					bNonRegularGrid, rhs, u, assAdapt);
	}
}

////////////////////////////////////////////////////////////////////////////////
// Assemble (instationary) Rhs
////////////////////////////////////////////////////////////////////////////////

/**
 * This function adds to the Rhs the entries of one subset
 * for all passed element discretizations.
 *
 * \param[in]		vElemDisc		element discretizations
 * \param[in]		dd				DoF Distribution
 * \param[in]		iterBegin		element iterator
 * \param[in]		iterEnd			element iterator
 * \param[in]		si				subset index
 * \param[in]		bNonRegularGrid flag to indicate if non regular grid is used
 * \param[in,out]	rhs				Right-hand side
 * \param[in]		vSol			current and previous solutions
 * \param[in]		vScaleMass		scaling factors for mass part
 * \param[in]		vScaleStiff		scaling factors for stiffness part
 * \param[in]		assAdapt		assemble adapter
 */
template <typename TElem, typename TDD, typename TAlgebra, typename TIterator>
void
AssembleRhs(	const std::vector<IElemDisc*>& vElemDisc,
               	ConstSmartPtr<TDD> dd,
				TIterator iterBegin,
				TIterator iterEnd,
               	int si, bool bNonRegularGrid,
               	typename TAlgebra::vector_type& rhs,
                ConstSmartPtr<VectorTimeSeries<typename TAlgebra::vector_type> > vSol,
               	const std::vector<number>& vScaleMass,
               	const std::vector<number>& vScaleStiff,
               	AssAdapter& assAdapt)
{
// 	check if at least on element exist, else return
	if(iterBegin == iterEnd) return;

// 	get marker
	BoolMarker* mark = assAdapt.pBoolMarker;
// 	check if assemble routine should be called only with respect to one index
	size_t assInd;
	if (assAdapt.assIndex.index_set)
		assInd = assAdapt.assIndex.index;

//	check time scheme
	if(vScaleMass.size() != vScaleStiff.size())
		UG_THROW("(instationary) AssembleRhs: s_a and s_m must have same size.");

	if(vSol->size() < vScaleStiff.size())
		UG_THROW("(instationary) AssembleRhs: Time stepping scheme needs at "
				"least "<<vScaleStiff.size()<<" time steps, but only "<<
				vSol->size() << " passed.");

//	get current time
	LocalVectorTimeSeries locTimeSeries;
	locTimeSeries.read_times(vSol);

//	prepare for given elem discs
	try
	{
	DataEvaluator Eval(MASS | STIFF | RHS,
	                   vElemDisc, dd->function_pattern(), si, bNonRegularGrid,
					   &locTimeSeries, &vScaleMass, &vScaleStiff);

//	prepare loop
	Eval.template prepare_elem_loop<TElem>();

//	local algebra
	LocalIndices ind; LocalVector locRhs, tmpLocRhs;

// 	Loop over all elements
	for(TIterator iter = iterBegin; iter != iterEnd; ++iter)
	{
	// 	get Element
		TElem* elem = *iter;

	//	check if elem is skipped from assembling
		if(mark) if(!mark->is_marked(elem)) continue;

	// 	get global indices
		dd->indices(elem, ind, Eval.use_hanging());

	// 	adapt local algebra
		locRhs.resize(ind); tmpLocRhs.resize(ind);

	//	read local values of time series
		locTimeSeries.read_values(vSol, ind);
		Eval.set_time_point(0);

	//	reset element contribution
		locRhs = 0.0;

	/////////////////////
	//	current time step

	//	get local solution at time point
		LocalVector& locU = locTimeSeries.solution(0);

	// 	prepare element
		try
		{
			Eval.prepare_elem(elem, locU, ind, false);
		}
		UG_CATCH_THROW("(instationary) AssembleRhs: Cannot prepare element.");

	// 	Assemble rhs
		try
		{
			tmpLocRhs = 0.0;
			Eval.add_rhs_elem(tmpLocRhs, elem, PT_INSTATIONARY);
			locRhs.scale_append(vScaleStiff[0], tmpLocRhs);

			Eval.add_rhs_elem(locRhs, elem, PT_STATIONARY);
		}
		UG_CATCH_THROW("(instationary) AssembleRhs: Cannot compute Rhs.");

	///////////////////
	//	old time steps

	//	loop all old time points
		for(size_t t = 1; t < vScaleStiff.size(); ++t)
		{
		//	get local solution at time point
			LocalVector& locU = locTimeSeries.solution(t);
			Eval.set_time_point(t);

		// 	prepare element
			try
			{
				Eval.prepare_elem(elem, locU, ind, false);
			}
			UG_CATCH_THROW("(instationary) AssembleRhs: Cannot prepare element.");

		// 	Assemble dM
			try
			{
				tmpLocRhs = 0.0;
				Eval.add_dM_elem(tmpLocRhs, locU, elem, PT_INSTATIONARY);
				locRhs.scale_append(-vScaleMass[t], tmpLocRhs);
			}
			UG_CATCH_THROW("(instationary) AssembleRhs: Cannot compute Jacobian (M).");

		// 	Assemble dA
			try
			{
				tmpLocRhs = 0.0;
				Eval.add_dA_elem(tmpLocRhs, locU, elem, PT_INSTATIONARY);
				locRhs.scale_append(-vScaleStiff[t], tmpLocRhs);
			}
			UG_CATCH_THROW("(instationary) AssembleRhs: Cannot compute Jacobian (A).");

		// 	Assemble rhs
			try
			{
				tmpLocRhs = 0.0;
				Eval.add_rhs_elem(tmpLocRhs, elem, PT_INSTATIONARY);
				locRhs.scale_append(vScaleStiff[t], tmpLocRhs);
			}
			UG_CATCH_THROW("(instationary) AssembleRhs: Cannot compute Rhs.");
		}

		// 	send local to global rhs
			if (assAdapt.assIndex.index_set){
				 /* the global vector is only set up with respect
				  *	to one global index 'assInd' -> rhs is a (block)-vector
				  *	at one index/DoF */
				AddLocalVectorAtIndex(rhs, locRhs, assInd);
			}
			else{
				AddLocalVector(rhs, locRhs);
			}
	}

// 	finish element loop
	try
	{
		Eval.finish_elem_loop();
	}
	UG_CATCH_THROW("(instationary) AssembleRhs: Cannot finish element loop.");

	}
	UG_CATCH_THROW("(instationary) AssembleRhs: Cannot create Data Evaluator.");
}

template <typename TElem, typename TDD, typename TAlgebra>
void
AssembleRhs(	const std::vector<IElemDisc*>& vElemDisc,
               	ConstSmartPtr<TDD> dd,
               	int si, bool bNonRegularGrid,
               	typename TAlgebra::vector_type& rhs,
                ConstSmartPtr<VectorTimeSeries<typename TAlgebra::vector_type> > vSol,
               	const std::vector<number>& vScaleMass,
               	const std::vector<number>& vScaleStiff,
               	AssAdapter& assAdapt)
{
	//	check if only some elements are selected
	if(assAdapt.pSelector)
	{
		Selector* sel = assAdapt.pSelector;
		const ISubsetHandler& sh = *dd->subset_handler();

		std::vector<TElem*> elems;
		for(typename Selector::traits<TElem>::iterator iter = sel->begin<TElem>();
			iter != sel->end<TElem>(); ++iter)
		{
			if(sh.get_subset_index(*iter) == si)
				elems.push_back(*iter);
		}

		//	assembling is carried out only over those elements
		//	which are selected and in subset si
		AssembleRhs<TElem,TDD,TAlgebra>
			(vElemDisc, dd, elems.begin(), elems.end(), si,
			 bNonRegularGrid, rhs, vSol, vScaleMass, vScaleStiff, assAdapt);
	}
	else
	{
		//	general case: assembling over all elements in subset si
		typedef typename TDD::template traits<TElem>::const_iterator const_iterator;

		const_iterator iterBegin = dd->template begin<TElem>(si);
		const_iterator iterEnd = dd->template end<TElem>(si);

		AssembleRhs<TElem, TDD, TAlgebra>
			(vElemDisc, dd, iterBegin, iterEnd, si,
					bNonRegularGrid, rhs, vSol, vScaleMass, vScaleStiff, assAdapt);
	}
}

////////////////////////////////////////////////////////////////////////////////
// Prepare Timestep (instationary)
////////////////////////////////////////////////////////////////////////////////

/**
 * This function calls the function "prepare_timestep_elem" of one subset for all passed
 * element discretizations.
 *
 * \param[in]		vElemDisc		element discretizations
 * \param[in]		dd				DoF Distribution
 * \param[in]		iterBegin		element iterator
 * \param[in]		iterEnd			element iterator
 * \param[in]		si				subset index
 * \param[in]		bNonRegularGrid flag to indicate if non regular grid is used
 * \param[in]		vSol			current and previous solutions
 * \param[in]		assAdapt		assemble adapter
 */
template <typename TElem, typename TDD, typename TAlgebra, typename TIterator>
void
PrepareTimestep(const std::vector<IElemDisc*>& vElemDisc,
               	ConstSmartPtr<TDD> dd,
				TIterator iterBegin,
				TIterator iterEnd,
               	int si, bool bNonRegularGrid,
                ConstSmartPtr<VectorTimeSeries<typename TAlgebra::vector_type> > vSol,
                AssAdapter& assAdapt)
{
// 	check if at least on element exist, else return
	if(iterBegin == iterEnd) return;

// 	get marker
	BoolMarker* mark = assAdapt.pBoolMarker;

//	get current time and vector
	const typename TAlgebra::vector_type& u = *vSol->solution(0);

//	create local time series
	LocalVectorTimeSeries locTimeSeries;
	locTimeSeries.read_times(vSol);

	try
	{
	DataEvaluator Eval(NONE,
	                   vElemDisc, dd->function_pattern(), si, bNonRegularGrid,
					   &locTimeSeries);
	Eval.set_time_point(0);

//	prepare element loop
	Eval.template prepare_elem_loop<TElem>();

//	local algebra
	LocalIndices ind; LocalVector locU;

// 	Loop over all elements
	for(TIterator iter = iterBegin; iter != iterEnd; ++iter)
	{
	// 	get Element
		TElem* elem = *iter;

	//	check if elem is skipped from assembling
		if(mark) if(!mark->is_marked(elem)) continue;

	// 	get global indices
		dd->indices(elem, ind, Eval.use_hanging());

	// 	adapt local algebra
		locU.resize(ind);

	// 	read local values of u
		GetLocalVector(locU, u);

	//	read local values of time series
		if(Eval.time_series_needed())
			locTimeSeries.read_values(vSol, ind);

	// 	prepare timestep
		try
		{
			Eval.prepare_timestep_elem(elem, locU);
		}
		UG_CATCH_THROW("(instationary) PrepareTimestep: Cannot prepare timestep.");
	}

	}
	UG_CATCH_THROW("(instationary) PrepareTimestep: Cannot create Data Evaluator.");
}

template <typename TElem, typename TDD, typename TAlgebra>
void
PrepareTimestep(const std::vector<IElemDisc*>& vElemDisc,
               	ConstSmartPtr<TDD> dd,
               	int si, bool bNonRegularGrid,
                ConstSmartPtr<VectorTimeSeries<typename TAlgebra::vector_type> > vSol,
                AssAdapter& assAdapt)
{
	//	check if only some elements are selected
	if(assAdapt.pSelector)
	{
		Selector* sel = assAdapt.pSelector;
		const ISubsetHandler& sh = *dd->subset_handler();

		std::vector<TElem*> elems;
		for(typename Selector::traits<TElem>::iterator iter = sel->begin<TElem>();
			iter != sel->end<TElem>(); ++iter)
		{
			if(sh.get_subset_index(*iter) == si)
				elems.push_back(*iter);
		}

		//	assembling is carried out only over those elements
		//	which are selected and in subset si
		PrepareTimestep<TElem,TDD,TAlgebra>
			(vElemDisc, dd, elems.begin(), elems.end(), si,
			 bNonRegularGrid, vSol, assAdapt);
	}
	else
	{
		//	general case: assembling over all elements in subset si
		typedef typename TDD::template traits<TElem>::const_iterator const_iterator;

		const_iterator iterBegin = dd->template begin<TElem>(si);
		const_iterator iterEnd = dd->template end<TElem>(si);

		PrepareTimestep<TElem, TDD, TAlgebra>
			(vElemDisc, dd, iterBegin, iterEnd, si,
					bNonRegularGrid, vSol, assAdapt);
	}
}

////////////////////////////////////////////////////////////////////////////////
// Finish Timestep (instationary)
////////////////////////////////////////////////////////////////////////////////

/**
 * This function calls the function "finish_timestep_elem" of one subset for all passed
 * element discretizations.
 *
 * \param[in]		vElemDisc		element discretizations
 * \param[in]		dd				DoF Distribution
 * \param[in]		iterBegin		element iterator
 * \param[in]		iterEnd			element iterator
 * \param[in]		si				subset index
 * \param[in]		bNonRegularGrid flag to indicate if non regular grid is used
 * \param[in]		vSol			current and previous solutions
 * \param[in]		assAdapt		assemble adapter
 */
template <typename TElem, typename TDD, typename TAlgebra, typename TIterator>
void
FinishTimestep(const std::vector<IElemDisc*>& vElemDisc,
               ConstSmartPtr<TDD> dd,
               TIterator iterBegin,
			   TIterator iterEnd,
               int si, bool bNonRegularGrid,
               ConstSmartPtr<VectorTimeSeries<typename TAlgebra::vector_type> > vSol,
               AssAdapter& assAdapt)
{
// 	check if at least on element exist, else return
	if(iterBegin == iterEnd) return;

// 	get marker
	BoolMarker* mark = assAdapt.pBoolMarker;

//	get current time and vector
	const typename TAlgebra::vector_type& u = *vSol->solution(0);

//	create local time series
	LocalVectorTimeSeries locTimeSeries;
	locTimeSeries.read_times(vSol);

//	prepare for given elem discs
	try
	{
	DataEvaluator Eval(NONE,
	                   vElemDisc, dd->function_pattern(), si , bNonRegularGrid,
					   &locTimeSeries);
	Eval.set_time_point(0);

//	prepare loop
	Eval.template prepare_elem_loop<TElem>();

//	local algebra
	LocalIndices ind; LocalVector locU;

// 	Loop over all elements
	for(TIterator iter = iterBegin; iter != iterEnd; ++iter)
	{
	// 	get Element
		TElem* elem = *iter;

	//	check if elem is skipped from assembling
		if(mark) if(!mark->is_marked(elem)) continue;

	// 	get global indices
		dd->indices(elem, ind, Eval.use_hanging());

	// 	adapt local algebra
		locU.resize(ind);

	// 	read local values of u
		GetLocalVector(locU, u);

	//	read local values of time series
		if(Eval.time_series_needed())
			locTimeSeries.read_values(vSol, ind);

	// 	finish timestep
		try
		{
			Eval.finish_timestep_elem(elem, locTimeSeries.time(0), locU);
		}
		UG_CATCH_THROW("(instationary) FinishTimestep: Cannot finish timestep.");
	}

	}
	UG_CATCH_THROW("(instationary) FinishTimestep: Cannot create Data Evaluator");
}

template <typename TElem, typename TDD, typename TAlgebra>
void
FinishTimestep(const std::vector<IElemDisc*>& vElemDisc,
               ConstSmartPtr<TDD> dd,
               int si, bool bNonRegularGrid,
               ConstSmartPtr<VectorTimeSeries<typename TAlgebra::vector_type> > vSol,
               AssAdapter& assAdapt)
{
	//	check if only some elements are selected
	if(assAdapt.pSelector)
	{
		Selector* sel = assAdapt.pSelector;
		const ISubsetHandler& sh = *dd->subset_handler();

		std::vector<TElem*> elems;
		for(typename Selector::traits<TElem>::iterator iter = sel->begin<TElem>();
			iter != sel->end<TElem>(); ++iter)
		{
			if(sh.get_subset_index(*iter) == si)
				elems.push_back(*iter);
		}

		//	assembling is carried out only over those elements
		//	which are selected and in subset si
		FinishTimestep<TElem,TDD,TAlgebra>
			(vElemDisc, dd, elems.begin(), elems.end(), si,
			 bNonRegularGrid, vSol, assAdapt);
	}
	else
	{
		//	general case: assembling over all elements in subset si
		typedef typename TDD::template traits<TElem>::const_iterator const_iterator;

		const_iterator iterBegin = dd->template begin<TElem>(si);
		const_iterator iterEnd = dd->template end<TElem>(si);

		FinishTimestep<TElem, TDD, TAlgebra>
			(vElemDisc, dd, iterBegin, iterEnd, si,
					bNonRegularGrid, vSol, assAdapt);
	}
}

} // end namespace ug


#endif /* __H__UG__LIB_DISC__SPATIAL_DISC__ELEM_DISC__ELEM_DISC_ASSEMBLE_UTIL__ */
