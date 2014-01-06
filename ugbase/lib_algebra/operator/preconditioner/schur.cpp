


// 01.02.2011 (m,d,y)

//	THIS FILE IS ONLY TEMPORARY!
//  When everything works as
//	expected one should move the code in this file to
//	schur_impl.h, so that it will work with all the different algebras.
//
//	In the moment template instantiations are invoked at the end of this file.

#ifdef UG_PARALLEL


// extern headers
#include <cmath>
#include <sstream>  // added for 'stringstream'

// algebra types
#include "lib_algebra/cpu_algebra_types.h"
#include "lib_algebra/operator/algebra_debug_writer.h"
#include "lib_algebra/parallelization/parallel_index_layout.h"

// own header
#include "schur.h"

// additions for profiling
#include "common/profiler/profiler.h"

#define PROFILE_SCHUR
#ifdef PROFILE_SCHUR
	#define SCHUR_PROFILE_FUNC()			PROFILE_FUNC_GROUP("algebra schur")
	#define SCHUR_PROFILE_BEGIN(name)	PROFILE_BEGIN_GROUP(name, "algebra schur")
	#define SCHUR_PROFILE_END()			PROFILE_END()
#else
	#define SCHUR_PROFILE_FUNC()
	#define SCHUR_PROFILE_BEGIN(name)
	#define SCHUR_PROFILE_END()
#endif
// additions for profiling - end

namespace ug{


template <class VT>
void UG_LOG_Vector(const VT &vec)
{
	for (size_t i=0; i<vec.size(); ++i)
	{ UG_LOG(vec[i] << " "); }
}

template <class MT>
void UG_LOG_Matrix(const MT &A)
{
	for (size_t i = 0; i<A.num_rows(); ++i)
	{
		UG_LOG ("{ "<<i<<": ");
		for(typename MT::const_row_iterator it = A.begin_row(i); it != A.end_row(i); ++it)
		{ UG_LOG ("("<< i << "," << it.index() <<" ) =" << A(i, it.index()) << std::endl); }
		UG_LOG ("}\n");
	}
};

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//	LocalSchurComplement implementation
template <typename TAlgebra>
void SchurComplementOperator<TAlgebra>::
init()
{
	UG_LOG("\n% 'SchurComplementOperator::init()':");
//	check that operator has been set
	if(m_spOperator.invalid())
		UG_THROW("SchurComplementOperator::init: No Operator A set.");

//	check Feti layouts have been set
//	if(m_pFetiLayouts == NULL)
//		UG_THROW("LocalSchurComplement::init: SCHUR layouts not set.");

//	save matrix from which we build the Schur complement
	typename TAlgebra::matrix_type &mat = m_spOperator->get_matrix();

	//	get matrix from dirichlet operator

	const SlicingData::slice_desc_type SD_INNER=SlicingData::SD_INNER;
	const SlicingData::slice_desc_type SD_SKELETON=SlicingData::SD_SKELETON;

	UG_LOG(*mat.layouts());
	// init sub matrices
	m_slicing.get_matrix(mat, SD_INNER, SD_INNER, sub_matrix(SD_INNER, SD_INNER));
	m_slicing.get_matrix(mat, SD_INNER, SD_SKELETON, sub_matrix(SD_INNER, SD_SKELETON));
	m_slicing.get_matrix(mat, SD_SKELETON, SD_INNER, sub_matrix(SD_SKELETON, SD_INNER));
	m_slicing.get_matrix(mat, SD_SKELETON, SD_SKELETON, sub_matrix(SD_SKELETON, SD_SKELETON));

	sub_matrix(SD_INNER, SD_INNER).set_storage_type(PST_ADDITIVE);
	sub_matrix(SD_INNER, SD_SKELETON).set_storage_type(PST_ADDITIVE);
	sub_matrix(SD_SKELETON, SD_INNER).set_storage_type(PST_ADDITIVE);
	sub_matrix(SD_SKELETON, SD_SKELETON).set_storage_type(PST_ADDITIVE);

	//	debug output of matrices
	UG_LOG("writing debug...")
	write_debug(sub_matrix(SD_INNER, SD_INNER), "Schur_II.mat");
	write_debug(sub_matrix(SD_INNER, SD_SKELETON), "Schur_IB.mat");
	write_debug(sub_matrix(SD_SKELETON, SD_INNER), "Schur_BI.mat");
	write_debug(sub_matrix(SD_SKELETON, SD_SKELETON), "Schur_BB.mat");

	//	init solver for Dirichlet problem
		if(m_spDirichletSolver.valid())
			if(!m_spDirichletSolver->init(sub_operator(SD_INNER, SD_INNER)))
				UG_THROW("LocalSchurComplement::init: Cannot init "
						"Dirichlet solver for operator A.");


//	reset apply counter
	m_applyCnt = 0;


} /* end 'LocalSchurComplement::init()' */


/// apply schur complement f_{\Gamma} = (A_{\Gamma, \Gamma} -  A_{\Gamma, I}  A_{I, I}^{-1}  A_{I, \Gamma} )u_{\Gamma}
template <typename TAlgebra>
void SchurComplementOperator<TAlgebra>::
apply(vector_type& fskeleton, const vector_type& uskeleton)
{
	UG_LOG("\n% 'SchurComplementOperator::apply()':");
	const SlicingData::slice_desc_type SD_INNER=SlicingData::SD_INNER;
	const SlicingData::slice_desc_type SD_SKELETON=SlicingData::SD_SKELETON;

	SCHUR_PROFILE_BEGIN(SCHUR_apply);
//	check that matrix has been set
	if(m_spOperator.invalid())
		UG_THROW("SchurComplementOperator::apply: Matrix A not set.");

//	check Dirichlet solver
	if(m_spDirichletSolver.invalid())
		UG_THROW("SchurComplementOperator::apply: No Dirichlet Solver set.");

//	Check parallel storage type of matrix
	if(!sub_matrix(SD_INNER, SD_INNER).has_storage_type(PST_ADDITIVE))
		UG_THROW("SchurComplementOperator::apply: Inadequate storage format of matrix.");

//	Check parallel storage type of vectors
	if (!uskeleton.has_storage_type(PST_CONSISTENT))
		UG_THROW("SchurComplementOperator::apply: Inadequate storage format of vec 'uskeleton' (should be consistent).");

	fskeleton.set(0.0);
	if(!fskeleton.has_storage_type(PST_ADDITIVE))
		UG_THROW("SchurComplementOperator::apply: Inadequate storage format of vec 'fskeleton' (should be additive).");

//	aux vectors
//	\todo: it would be sufficient to copy only the layouts without copying the values
	const int n_inner = sub_size(SD_INNER);
	vector_type uinner; uinner.create(n_inner);
	vector_type finner; finner.create(n_inner);

	// storage type does not matter, but is required for solver


	// f_{\Gamma} = (A_{\Gamma, \Gamma} -  A_{\Gamma, I}  A_{I, I}^{-1}  A_{I, \Gamma} )u_{\Gamma}


	// compute first contribution
	UG_LOG("U_Gamma="); UG_LOG_Vector(uskeleton);
	UG_LOG("A_Gamma,Gamma="); UG_LOG_Matrix(*sub_operator(SD_SKELETON, SD_SKELETON));

	sub_operator(SD_SKELETON, SD_SKELETON)->apply(fskeleton, uskeleton);

	UG_LOG("fskeleton="); UG_LOG_Vector<vector_type>(fskeleton);

	if(debug_writer().valid())
	{
		//	Debug output of vector
		//	add iter count to name
			std::string name("Schur ");
			char ext[20]; sprintf(ext, "_a_rhs_skeleton%03d.vec", m_applyCnt);
			name.append(ext);
			//UG_LOG("fskelton="<<fskeleton);
			//debug_writer()->write_vector(fskeleton, name.c_str());
	}



	UG_LOG("\nfskeleton="); UG_LOG_Vector<vector_type>(fskeleton);

	//fskeleton.set_storage_type(PST_ADDITIVE);
	//uTmp.set_storage_type(PST_CONSISTENT);
	// compute second contribution
    // no communication in the interior !
	sub_operator(SD_INNER, SD_SKELETON)->apply(finner, uskeleton);
	finner.set_storage_type(PST_ADDITIVE);
	uinner.set_storage_type(PST_CONSISTENT);

	if(!m_spDirichletSolver->apply_return_defect(uinner, finner))
	{
		UG_LOG_ALL_PROCS("ERROR in 'SchurComplementOperator::apply': "
						 "Could not solve Dirichlet problem (step 3.b) on Proc "
							<< pcl::GetProcRank() << /*" (m_statType = '" << m_statType <<*/ "').\n");
		UG_LOG_ALL_PROCS("ERROR in 'SchurComplementOperator::apply':"
						" Last defect was " << m_spDirichletSolver->defect() <<
						" after " << m_spDirichletSolver->step() << " steps.\n");

		UG_THROW("Cannot solve Local Schur Complement.");
	}

	// modify fskeleton
	sub_operator(SD_SKELETON, SD_INNER)->apply_sub(fskeleton, uinner);


	m_applyCnt++;
} /* end 'LocalSchurComplement::apply()' */

template <typename TAlgebra>
void SchurComplementOperator<TAlgebra>::
apply_sub(vector_type& fskeleton, const vector_type& uskeleton)
{
	UG_LOG("\n% 'SchurComplementOperator::apply_sub()':");
	SCHUR_PROFILE_BEGIN(SCHUR_apply_sub);
//	create new rhs
	vector_type dskeleton(fskeleton);
	//dskeleton.resize(fskeleton.size());

//	solve
	apply(dskeleton, uskeleton);

//	subtract from vector
	fskeleton -= dskeleton;
}





////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//	SchurSolver implementation
template <typename TAlgebra>
SchurPrecond<TAlgebra>::SchurPrecond() :
	//m_spOperator(NULL),
	m_spSchurComplementOp(NULL),
	m_spSkeletonMatrix(NULL),
	m_spDirichletSolver(NULL),
	m_spSkeletonSolver(NULL)
{
	// clear aux vector smart ptrs
	// (will be initialized in first step)
	m_aux_rhs[0] = m_aux_rhs[1] =NULL;
	m_aux_sol[0] = m_aux_sol[1] = NULL;
}


template <typename TAlgebra>
bool SchurPrecond<TAlgebra>::
postprocess()
{
	// clear aux vector smart ptrs
	// (were initialized in first step)
	m_aux_rhs[0] = m_aux_rhs[1] =NULL;
	m_aux_sol[0] = m_aux_sol[1] = NULL;
}

template <typename TAlgebra>
bool SchurPrecond<TAlgebra>::
preprocess(SmartPtr<MatrixOperator<matrix_type, vector_type> > A)
{
//	status
	UG_LOG("\n% Initializing SCHUR precond: \n");

//	bool flag
	bool bSuccess = true;

//	remember A
	//m_spOperator = A;

//	0. get matrix
	//matrix_type *m_pMatrix = &m_spOperator->get_matrix();
	matrix_type &Amat = A->get_matrix();
	const int N = Amat.num_rows();



//	check that DDInfo has been set
/*	if(m_pDDInfo == NULL)
	{
		UG_LOG("ERROR in SchurSolver::init: DDInfo not set.\n");
		return false;
	}*/

	bool debugLayouts = (debug_writer()==NULL) ? false : true;
// Determine splitting


//  ----- 1. CONFIGURE LOCAL SCHUR COMPLEMENT  ----- //

//	1.1 Determine slicing for SchurComplementOperator
	ConstSmartPtr<AlgebraLayouts> layouts = Amat.layouts();
	std::vector<SlicingData::slice_desc_type> skeletonMark(N, SlicingData::SD_INNER);
	MarkAllFromLayout<SlicingData::slice_desc_type> (skeletonMark, layouts->master(), SlicingData::SD_SKELETON);
	MarkAllFromLayout(skeletonMark, layouts->slave(), SlicingData::SD_SKELETON);

//	1.2 init Dirichlet system solver
	if(m_spDirichletSolver.invalid())
	{
		UG_LOG("ERROR in SchurSolver::init: No dirichlet solver set "
				" for inversion of A_{II} in Local Schur complement.\n");
		return false;
	}

	if(m_spSkeletonSolver.invalid())
	{
			UG_LOG("ERROR in SchurPrecond::init: No skeleton solver set.\n");
			return false;
	}

//	1.3 create & init local Schur complement object
	m_spSchurComplementOp = new SchurComplementOperator<TAlgebra>(A, skeletonMark);

//	set dirichlet solver for local Schur complement
	m_spSchurComplementOp->set_dirichlet_solver(m_spDirichletSolver);

//	init
	UG_LOG("\n%   - Init local Schur complement ... ");
	SCHUR_PROFILE_BEGIN(SchurPrecondInit_InitLocalSchurComplement);
	m_spSchurComplementOp->init();
	UG_LOG("done.\n");

	//if (debug_writer().valid())
	//		m_spSchurComplementOp->set_debug(debug_writer());

//	1.4 check all procs
	if(!pcl::AllProcsTrue(bSuccess))
	{
		UG_LOG("ERROR in SchurPrecond::init: Some processes could not init"
				" local Schur complement.\n");
		return false;
	}
	SCHUR_PROFILE_END();			// end 'SCHUR_PROFILE_BEGIN(SchurPrecondInit_InitLocalSchurComplement)' - Messpunkt ok!

//  ----- 2. CONFIGURE SCHUR COMPLEMENT SOLVER  ----- //

	m_spSkeletonMatrix = m_spSchurComplementOp->sub_operator(1,1);
	if(!m_spSkeletonSolver->init(m_spSchurComplementOp))
		UG_THROW("SchurPrecond::init: Failed to init skeleton solver.");

//	2.5 check all procs
/*	if(!pcl::AllProcsTrue(bSuccess))
	{
		UG_LOG("ERROR in SchurPrecond::init: Some processes could not init"
				" Schur complement inverse.\n");
		return false;
	}
	*/
	//SCHUR_PROFILE_END();			// end 'SCHUR_PROFILE_BEGIN(SchurPrecondInit_InitPrimalSubassMatInv)' - Messpunkt ok!

//	status
	UG_LOG("\n% 'SchurPrecond::init()' done!\n");

//	we're done
	return true;
} /* end 'SchurPrecond::preprocess()' */




//	Stepping routine


template <typename TAlgebra>
bool SchurPrecond<TAlgebra>::
step(SmartPtr<MatrixOperator<matrix_type, vector_type> > pOp, vector_type& c, const vector_type& d)
{

	bool bSuccess = true;	//	status

	c.set_storage_type(PST_UNIQUE);

	UG_LOG("\n% 'SchurPrecond::step()':");

	const SlicingData::slice_desc_type SD_INNER=SlicingData::SD_INNER;
	const SlicingData::slice_desc_type SD_SKELETON=SlicingData::SD_SKELETON;

	const size_t n_inner=m_spSchurComplementOp->sub_size(SD_INNER);
	const size_t n_skeleton=m_spSchurComplementOp->sub_size(SD_SKELETON);

	//	check storage type
	if(!d.has_storage_type(PST_ADDITIVE))
	{
		UG_LOG("ERROR: In 'SchurPrecond::step':Inadequate storage format of 'd'.\n");
		return false;
	}

	//
	if (n_skeleton == 0)
	{
		UG_LOG("\n% 'SchurPrecond::step() - direct solve':");
		c.set_storage_type(PST_CONSISTENT);
		m_spDirichletSolver->apply(c, d);
		return bSuccess;
	}

	const SlicingData sd =m_spSchurComplementOp->slicing();
	UG_ASSERT(n_skeleton > 0, "HUHH: # skeleton dof should be positive ");

	// now we have a skeleton

	if (m_aux_rhs[SD_SKELETON].invalid())
	{
		UG_LOG("\n% Creating skeleton defect vector of size " << n_skeleton << std::endl);
		//m_aux_rhs[SD_SKELETON] = new vector_type(n_skeleton);
		m_aux_rhs[SD_SKELETON] = sd.slice_clone_without_values(d, SD_SKELETON);
		m_aux_rhs[SD_SKELETON]->set_storage_type(PST_ADDITIVE);

		UG_LOG("Skeleton f:\n" <<*m_aux_rhs[SD_SKELETON]->layouts());
	}

	if (m_aux_sol[SD_SKELETON].invalid())
	{
		UG_LOG("\n% Creating skeleton corr vector of size " << n_skeleton << std::endl);
		//m_aux_sol[SD_SKELETON] = new vector_type(n_skeleton);
		m_aux_sol[SD_SKELETON] = sd.slice_clone_without_values(d, SD_SKELETON);
		m_aux_sol[SD_SKELETON]->set_storage_type(PST_CONSISTENT);

		UG_LOG("Skeleton u:\n" << *m_aux_sol[SD_SKELETON]->layouts());
	}

	if (m_aux_rhs[SD_INNER].invalid())
	{
		UG_LOG("\n% Creating inner defect vector of size " << n_inner << std::endl);
		m_aux_rhs[SD_INNER] = new vector_type(n_inner);
		m_aux_rhs[SD_INNER]->set_storage_type(PST_ADDITIVE);
	}

	if (m_aux_sol[SD_INNER].invalid())
	{
			UG_LOG("\n% Creating inner corr vector of size " << n_inner << std::endl);
			m_aux_sol[SD_INNER] = new vector_type(n_inner);
			m_aux_sol[SD_INNER]->set_storage_type(PST_CONSISTENT);
	}



	// create vector short cuts
	vector_type &f_skeleton=*m_aux_rhs[SD_SKELETON];
	vector_type &u_skeleton=*m_aux_sol[SD_SKELETON];

	vector_type &f_inner=*m_aux_rhs[SD_INNER];
	vector_type &u_inner=*m_aux_sol[SD_INNER];


	// forward solve
	UG_LOG("\n% 'SchurPrecond::step() - forward':");
	SCHUR_PROFILE_BEGIN(SchurSolverStep_Forward);
	m_spDirichletSolver->apply_return_defect(u_inner, f_inner);
	m_spSchurComplementOp->slicing().set_vector_slice(u_inner, c, SD_INNER);
	// now u_inner may used again

	m_spSchurComplementOp->sub_operator(SD_SKELETON, SD_INNER)->apply(f_skeleton, u_inner);
	m_spSchurComplementOp->slicing().subtract_vector_slice(d, SD_SKELETON, f_skeleton);
	f_skeleton *= -1.0;

	SCHUR_PROFILE_END(); // end 'SchurSolverApply_Forward' - Messpunkt ok, da Konvergenz-Check ausgefuehrt

	// (preconditioned) skeleton solve
	UG_LOG("\n% 'SchurPrecond::step() - skeleton solve':");
	SCHUR_PROFILE_BEGIN(SchurSolverStep_SchurSolve);
	u_skeleton.set(0.0);
	m_spSkeletonSolver->apply(u_skeleton, f_skeleton);
	SCHUR_PROFILE_END();

	// backward solve
	UG_LOG("\n% 'SchurPrecond::step() - backward':");
	SCHUR_PROFILE_BEGIN(SchurSolverStep_Backward);
	m_spSchurComplementOp->sub_operator(SD_INNER, SD_SKELETON)->apply(f_inner, u_skeleton);
	m_spDirichletSolver->apply_return_defect(u_inner, f_inner);
	m_spSchurComplementOp->slicing().subtract_vector_slice(u_inner, c, SD_INNER);

//	check all procs
	if(!pcl::AllProcsTrue(bSuccess))
	{
		UG_LOG("ERROR in SchurSolver::apply: Some processes could not back solve.\n");
		return false;
	}
	SCHUR_PROFILE_END();			// end 'SchurSolver_Backward'


	return bSuccess;

//	call this for output.
//	PROFILER_UPDATE();
//	PROFILER_OUTPUT("feti_profiling.rtf");
} /* end 'SchurPrecond::step()' */




////////////////////////////////////////////////////////////////////////
//	template instantiations for all current algebra types.




#ifdef UG_CPU_1
template class SchurComplementOperator<CPUAlgebra>;
template class SchurPrecond<CPUAlgebra>;
#endif
#ifdef UG_CPU_2
template class SchurComplementOperator<CPUBlockAlgebra<2> >;
template class SchurPrecond<CPUBlockAlgebra<2> >;
#endif
#ifdef UG_CPU_3
template class SchurComplementOperator<CPUBlockAlgebra<3> >;
template class SchurPrecond<CPUBlockAlgebra<3> >;
#endif

};  // end of namespace

#endif
