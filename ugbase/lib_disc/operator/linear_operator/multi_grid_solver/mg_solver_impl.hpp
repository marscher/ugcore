/*
 * mg_solver_impl.h
 *
 *  Created on: 04.01.2010
 *      Author: andreasvogel
 */

#ifndef __H__UG__LIB_DISC__MULTI_GRID_SOLVER__MG_SOLVER_IMPL__
#define __H__UG__LIB_DISC__MULTI_GRID_SOLVER__MG_SOLVER_IMPL__

#include <iostream>
#include <sstream>
#include <string>
#include "common/profiler/profiler.h"
#include "lib_disc/function_spaces/grid_function_util.h"
#include "lib_disc/operator/linear_operator/prolongation_operator.h"
#include "lib_disc/operator/linear_operator/projection_operator.h"

#include "mg_solver.h"

#ifdef UG_PARALLEL
	#include "lib_algebra/parallelization/parallelization.h"
	#include "pcl/pcl_util.h"
//	the debug barrier is used to eliminate synchronization overhead from
//	profiling stats. Only used for parallel builds.
//	PCL_DEBUG_BARRIER only has an effect if PCL_DEBUG_BARRIER_ENABLED is defined.
	#define GMG_PARALLEL_DEBUG_BARRIER(comm) PCL_DEBUG_BARRIER(comm)

#else
	#define GMG_PARALLEL_DEBUG_BARRIER(comm)
#endif

#define PROFILE_GMG
#ifdef PROFILE_GMG
	#define GMG_PROFILE_FUNC()		PROFILE_FUNC()
	#define GMG_PROFILE_BEGIN(name)	PROFILE_BEGIN_GROUP(name, "gmg")
	#define GMG_PROFILE_END()		PROFILE_END()
#else
	#define GMG_PROFILE_FUNC()
	#define GMG_PROFILE_BEGIN(name)
	#define GMG_PROFILE_END()
#endif

namespace ug{

////////////////////////////////////////////////////////////////////////////////
// Constructor
////////////////////////////////////////////////////////////////////////////////

template <typename TDomain, typename TAlgebra>
AssembledMultiGridCycle<TDomain, TAlgebra>::
AssembledMultiGridCycle(SmartPtr<ApproximationSpace<TDomain> > approxSpace) :
	m_spSurfaceMat(NULL), m_spAss(NULL), m_spApproxSpace(approxSpace),
	m_topLev(GridLevel::TOP), m_surfaceLev(GridLevel::TOP),
	m_baseLev(0), m_bBaseParallel(true), m_cycleType(1),
	m_numPreSmooth(2), m_numPostSmooth(2),
	m_bAdaptive(true),
	m_spPreSmootherPrototype(new Jacobi<TAlgebra>()),
	m_spPostSmootherPrototype(m_spPreSmootherPrototype),
	m_spProjectionPrototype(new InjectionTransfer<TDomain,TAlgebra>(m_spApproxSpace)),
	m_spProlongationPrototype(new StdTransfer<TDomain,TAlgebra>(m_spApproxSpace)),
	m_spRestrictionPrototype(m_spProlongationPrototype),
	m_spBaseSolver(new LU<TAlgebra>()),
	m_NonGhostMarker(*m_spApproxSpace->domain()->grid()),
	m_spDebugWriter(NULL), m_dbgIterCnt(0)
{};

template <typename TDomain, typename TAlgebra>
SmartPtr<ILinearIterator<typename TAlgebra::vector_type> >
AssembledMultiGridCycle<TDomain, TAlgebra>::
clone()
{
	SmartPtr<AssembledMultiGridCycle<TDomain, TAlgebra> > clone(
		new AssembledMultiGridCycle<TDomain, TAlgebra>(m_spApproxSpace));

	clone->set_base_level(m_baseLev);
	clone->set_base_solver(m_spBaseSolver);
	clone->set_cycle_type(m_cycleType);
	clone->set_debug(m_spDebugWriter);
	clone->set_discretization(m_spAss);
	clone->set_num_postsmooth(m_numPostSmooth);
	clone->set_num_presmooth(m_numPreSmooth);
	clone->set_projection(m_spProjectionPrototype);
	clone->set_prolongation(m_spProlongationPrototype);
	clone->set_restriction(m_spRestrictionPrototype);
	clone->set_presmoother(m_spPreSmootherPrototype);
	clone->set_postsmoother(m_spPostSmootherPrototype);
	clone->set_surface_level(m_surfaceLev);

	for(size_t i = 0; i < m_vspProlongationPostProcess.size(); ++i)
		clone->add_prolongation_post_process(m_vspProlongationPostProcess[i]);

	for(size_t i = 0; i < m_vspRestrictionPostProcess.size(); ++i)
		clone->add_restriction_post_process(m_vspRestrictionPostProcess[i]);

	return clone;
}

template <typename TDomain, typename TAlgebra>
AssembledMultiGridCycle<TDomain, TAlgebra>::
~AssembledMultiGridCycle()
{};

////////////////////////////////////////////////////////////////////////////////
// apply and init
////////////////////////////////////////////////////////////////////////////////

template <typename TDomain, typename TAlgebra>
bool AssembledMultiGridCycle<TDomain, TAlgebra>::
apply(vector_type &c, const vector_type& d)
{
	PROFILE_FUNC_GROUP("gmg");

	try{
// 	Check if surface level has been chosen correctly
//	Please note, that the approximation space returns the global number of levels,
//	i.e. the maximum of levels among all processes.
	if(m_topLev >= (int)m_spApproxSpace->num_levels())
		UG_THROW("AssembledMultiGridCycle::apply: SurfaceLevel "<<m_topLev<<" does not exist.");

// 	Check if base level has been choose correctly
	if(m_baseLev > m_topLev)
		UG_THROW("AssembledMultiGridCycle::apply: Base level must be smaller or equal to surface Level.");

//	debug output
	write_surface_debug(d, "GMG_Defect_In");

//	project defect from surface to level
	GMG_PROFILE_BEGIN(GMG_ProjectDefectFromSurface);
	try{
		for(size_t lev = 0; lev < m_vLevData.size(); ++lev){
			if(m_vLevData[lev]->d.valid()){
				// \todo: Check why setting to zero is necessary in adaptive
				//		  case. This should not be the case
				m_vLevData[lev]->d->set(0.0);
				#ifdef UG_PARALLEL
				m_vLevData[lev]->d->set_storage_type(d.get_storage_mask());
				#endif
			}
		}

		for(size_t i = 0; i < m_vSurfToLevelMap.size(); ++i){
			const int level = m_vSurfToLevelMap[i].level;
			const size_t index = m_vSurfToLevelMap[i].index;

			(*(m_vLevData[level]->d))[index] = d[i];
		}
	}
	UG_CATCH_THROW("AssembledMultiGridCycle::apply: Project d Surf -> Level failed.");
	GMG_PROFILE_END(); //GMGApply_ProjectDefectFromSurface

// 	Perform one multigrid cycle
//	At this point c, d are valid for m_vLevData[m_topLev]->c, m_vLevData[m_topLev]->d
	GMG_PROFILE_BEGIN(GMG_lmgc);
	UG_DLOG(LIB_DISC_MULTIGRID, 4, "gmg-apply lmgc (on level " << m_topLev << ")... \n");
	try{
		lmgc(m_topLev);
	}
	UG_CATCH_THROW("AssembledMultiGridCycle: lmgc failed.");
	GMG_PROFILE_END(); //GMGApply_lmgc

//	project correction from level to surface
	GMG_PROFILE_BEGIN(GMG_ProjectCorrectionFromLevelToSurface);
	UG_DLOG(LIB_DISC_MULTIGRID, 4, "gmg-apply project_level_to_surface... \n");
	try{
		for(size_t i = 0; i < m_vSurfToLevelMap.size(); ++i){
			const int level = m_vSurfToLevelMap[i].level;
			const size_t index = m_vSurfToLevelMap[i].index;

			 c[i] = (*(m_vLevData[level]->c))[index];
		}

		#ifdef UG_PARALLEL
		c.set_storage_type(PST_CONSISTENT);
		#endif
	}
	UG_CATCH_THROW("AssembledMultiGridCycle::apply: Project c Level -> Surface failed.");
	GMG_PROFILE_END(); //GMGApply_ProjectCorrectionFromLevelToSurface

//	apply scaling
	const number kappa = this->damping()->damping(c, d, m_spSurfaceMat.template cast_dynamic<ILinearOperator<vector_type> >());
	if(kappa != 1.0) c *= kappa;

//	debug output
	write_surface_debug(c, "GMG_Correction_Out");
	write_surface_debug(*m_spSurfaceMat, "GMG_SurfaceStiffness");

//	increase dbg counter
	if(m_spDebugWriter.valid()) m_dbgIterCnt++;

	} UG_CATCH_THROW("AssembledMultiGridCycle::apply: Application failed.");

	UG_DLOG(LIB_DISC_MULTIGRID, 4, "gmg-apply done. \n");
	return true;
}

template <typename TDomain, typename TAlgebra>
bool AssembledMultiGridCycle<TDomain, TAlgebra>::
apply_update_defect(vector_type &c, vector_type& d)
{
	PROFILE_FUNC_GROUP("gmg");

//	NOTE: 	This is the implementation of a multiplicative Multigrid. Thus, the
//			defect is kept up to date when traversing the grid. At the end of
//			the iteration the updated defect is stored in the level defects and
//			could be projected back to the surface in order to get an updated
//			surface defect. This is, however, not done. For these reasons:
//			a) A Matrix-Vector operation is not very expensive
//			b) In a 2d adaptive case, the update is difficult, but can be
//				performed. But if the implementation is incorrect, this will
//				hardly be detected.
//			c) In a 3d adaptive case, it is impossible to ensure, that assembled
//				level matrices and the surface matrix have the same couplings
//				(not even to inner points). This is due to the fact, that e.g.
//				finite volume geometries are computed using different
//				integration points. (Hanging fv used triangles as scvf in 3d,
//				while normal fv use quads). Therefore, the updated defect is
//				only approximately the correct defect. In order to return the
//				correct defect, we must recompute the defect in the
//				adaptive case.
//			d) If scaling of the correction is performed, the defect must be
//				recomputed anyway. Thus, only scale=1.0 allows optimizing.
//			e) Updated defects are only needed in LinearIterativeSolvers. In
//				Krylov-Methods (CG, BiCGStab, ...) only the correction is
//				needed. We optimize for that case.

//	compute correction
	if(!apply(c, d)) return false;

//	update defect: d = d - A*c
	m_spSurfaceMat->matmul_minus(d, c);

//	write for debugging
	write_surface_debug(d, "GMG_Defect_Out");

	return true;
}

template <typename TDomain, typename TAlgebra>
bool AssembledMultiGridCycle<TDomain, TAlgebra>::
init(SmartPtr<ILinearOperator<vector_type> > J, const vector_type& u)
{
	PROFILE_FUNC_GROUP("gmg");
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-start - init(J, u)\n");

	// try to extract assembling routine
	SmartPtr<AssembledLinearOperator<TAlgebra> > spALO =
			J.template cast_dynamic<AssembledLinearOperator<TAlgebra> >();
	if(spALO.valid()){
		m_spAss = spALO->discretization();
	}

	// Store Surface Matrix
	m_spSurfaceMat = J.template cast_dynamic<matrix_type>();

	// Store Surface Solution
	m_pSurfaceSol = &u;

	// call init
	init();

	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop - init(J, u)\n");
	return true;
}

template <typename TDomain, typename TAlgebra>
bool AssembledMultiGridCycle<TDomain, TAlgebra>::
init(SmartPtr<ILinearOperator<vector_type> > L)
{
	PROFILE_FUNC_GROUP("gmg");
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-start - init(L)\n");

	// try to extract assembling routine
	SmartPtr<AssembledLinearOperator<TAlgebra> > spALO =
			L.template cast_dynamic<AssembledLinearOperator<TAlgebra> >();
	if(spALO.valid()){
		m_spAss = spALO->discretization();
	}

	// Store Surface Matrix
	m_spSurfaceMat = L.template cast_dynamic<matrix_type>();

	// Store Surface Solution
	m_pSurfaceSol = NULL;

	// call init
	init();

	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop - init(L)\n");
	return true;
}



template <typename TDomain, typename TAlgebra>
void AssembledMultiGridCycle<TDomain, TAlgebra>::
init()
{
	PROFILE_FUNC_GROUP("gmg");
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-start init_common\n");

	try{

// 	Cast Operator
	if(m_spSurfaceMat.invalid())
		UG_THROW("AssembledMultiGridCycle:init: Can not cast Operator to Matrix.");

//	Check Approx Space
	if(m_spApproxSpace.invalid())
		UG_THROW("AssembledMultiGridCycle::init: Approximation Space not set.");

//	check that grid given
	if(m_spApproxSpace->num_levels() == 0)
		UG_THROW("AssembledMultiGridCycle:init: No grid levels");

	if(m_spAss.invalid())
		UG_THROW("AssembledMultiGridCycle::init: Discretization not set.");

	if(m_spBaseSolver.invalid())
		UG_THROW("AssembledMultiGridCycle::init: Base Solver not set.");

	if(m_spPreSmootherPrototype.invalid())
		UG_THROW("AssembledMultiGridCycle::init: PreSmoother not set.");

	if(m_spPostSmootherPrototype.invalid())
		UG_THROW("AssembledMultiGridCycle::init: PostSmoother not set.");

	if(m_spProlongationPrototype.invalid())
		UG_THROW("AssembledMultiGridCycle::init: Prolongation not set.");

	if(m_spRestrictionPrototype.invalid())
		UG_THROW("AssembledMultiGridCycle::init: Restriction not set.");

//	get current toplevel
	const GridFunction<TDomain, TAlgebra>* pSol =
		dynamic_cast<const GridFunction<TDomain, TAlgebra>*>(m_pSurfaceSol);
	if(pSol){
		m_surfaceLev = pSol->dof_distribution()->grid_level().level();
	}

	if(m_surfaceLev != GridLevel::TOP) m_topLev = m_surfaceLev;
	else m_topLev = m_spApproxSpace->num_levels() - 1;

	if(m_baseLev > m_topLev)
		UG_THROW("AssembledMultiGridCycle::init: Base Level greater than Surface level.");

//	check, if grid is full-refined
//todo:	make sure that there are no vertical masters in topLevel. Otherwise
//		the grid can not be considered fully refined.
//todo: Even if there are vrtMasters and m_bFullRefined is false and the top
//		level matrix can't be copied, an injective SurfToTopLevMapPatchToGlobal might be useful...
//todo:	Eventually the multigrid is only executed on a subset of processes.
//		A process communicator would thus make sense, which defines this subset.
//		Use that in the call below.
	if(m_spApproxSpace->dof_distribution(GridLevel(m_topLev, GridLevel::LEVEL, true))->num_indices() ==
		m_spApproxSpace->dof_distribution(GridLevel(m_surfaceLev, GridLevel::SURFACE))->num_indices())
	{
		UG_DLOG(LIB_DISC_MULTIGRID, 4, "init_common - local grid is non adaptive\n");
		m_bAdaptive = false;
	}
	else{
		UG_DLOG(LIB_DISC_MULTIGRID, 4, "init_common - local grid is adaptive: ");
		m_bAdaptive = true;
	}
	#ifdef UG_PARALLEL
	m_bAdaptive = pcl::OneProcTrue(m_bAdaptive);
	#endif

//	Allocate memory for given top level
	try{
		top_level_required(m_topLev);
	}
	UG_CATCH_THROW("AssembledMultiGridCycle::init: Cannot allocate memory.");

//	init mapping from surface level to top level in case of full refinement
	GMG_PROFILE_BEGIN(GMG_InitSurfToLevelMapping);
	try{
		init_surface_to_level_mapping();
	}
	UG_CATCH_THROW("AssembledMultiGridCycle: Cannot create SurfaceToLevelMap.")
	GMG_PROFILE_END();

//	Assemble coarse grid operators
	GMG_PROFILE_BEGIN(GMG_AssembleLevelGridOperator);
	try{
		init_level_operator();
	}
	UG_CATCH_THROW("AssembledMultiGridCycle: Initialization of Level Operator failed.");
	GMG_PROFILE_END();

//	Init smoother for coarse grid operators
	GMG_PROFILE_BEGIN(GMG_InitSmoother);
	try{
		init_smoother();
	}
	UG_CATCH_THROW("AssembledMultiGridCycle:init: Cannot init Smoother.");
	GMG_PROFILE_END();

//	Init base solver
	GMG_PROFILE_BEGIN(GMG_InitBaseSolver);
	try{
		init_base_solver();
	}
	UG_CATCH_THROW("AssembledMultiGridCycle:init: Cannot init Base Solver.");
	GMG_PROFILE_END();

// 	Create Interpolation
	GMG_PROFILE_BEGIN(GMG_InitProlongation);
	try{
		init_transfer();
	}
	UG_CATCH_THROW("AssembledMultiGridCycle:init: Cannot init Transfer (Prolongation/Restriction).");
	GMG_PROFILE_END();

	} UG_CATCH_THROW("AssembledMultiGridCycle: Init failure for init(u)");

	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop init_common\n");
}


template <typename TDomain, typename TAlgebra>
void AssembledMultiGridCycle<TDomain, TAlgebra>::
init_level_operator()
{
	PROFILE_FUNC_GROUP("gmg");
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-start init_linear_level_operator\n");

//	Create Projection
	GMG_PROFILE_BEGIN(GMG_ProjectSurfaceSolution);
	try{
		if(m_pSurfaceSol) {

			if(!m_spProjectionPrototype.valid())
				UG_THROW("AssembledMultiGridCycle::init: Projection not set.");

			init_projection();

			for(size_t lev = 0; lev < m_vLevData.size(); ++lev){
				if(m_vLevData[lev]->u.valid()){
					// \todo: Check why setting to zero is necessary in adaptive
					//		  case. This should not be the case
					m_vLevData[lev]->u->set(0.0);
					#ifdef UG_PARALLEL
					m_vLevData[lev]->u->set_storage_type(m_pSurfaceSol->get_storage_mask());
					#endif
				}
			}

			for(size_t i = 0; i < m_vSurfToLevelMap.size(); ++i){
				const int level = m_vSurfToLevelMap[i].level;
				const size_t index = m_vSurfToLevelMap[i].index;

				(*(m_vLevData[level]->u))[index] = (*m_pSurfaceSol)[i];
			}

			GMG_PROFILE_BEGIN(GMG_ProjectSolutionDown);
			for(int lev = m_topLev; lev != m_baseLev; --lev)
			{
				#ifdef UG_PARALLEL
					copy_to_vertical_masters(*m_vLevData[lev]->u);
				#endif

			//	skip void level
				if(m_vLevData[lev]->num_indices() == 0 ||
					m_vLevData[lev-1]->num_indices() == 0) continue;

				try{
					m_vLevData[lev]->Projection->do_restrict(*m_vLevData[lev-1]->u, *m_vLevData[lev]->u);
				} UG_CATCH_THROW("AssembledMultiGridCycle::init: Cannot project "
							"solution to coarse grid function of level "<<lev-1<<".\n");
			}
			#ifdef UG_PARALLEL
				if(m_baseLev != m_topLev){
					copy_to_vertical_masters(*m_vLevData[m_baseLev]->u);
				}
			#endif
			GMG_PROFILE_END();
		}
	}
	UG_CATCH_THROW("AssembledMultiGridCycle::init: Projection of Surface Solution failed.");
	GMG_PROFILE_END();

// 	Create coarse level operators
	for(size_t lev = m_baseLev; lev < m_vLevData.size(); ++lev)
	{
	//	skip void level
		if(m_vLevData[lev]->num_indices() == 0) continue;

		GMG_PROFILE_BEGIN(GMG_AssLevelMat);
	//	if ghosts are present we have to assemble the matrix only on non-ghosts
	//	for the smoothing matrices
		if(m_vLevData[lev]->has_ghosts())
		{
		//	set this selector to the assembling, such that only those elements
		//	will be assembled and force grid to be considered as regular
			m_spAss->ass_tuner()->set_marker(&m_NonGhostMarker);
			m_spAss->ass_tuner()->set_force_regular_grid(true);

		//	init level operator
			try{
			m_spAss->assemble_jacobian(*m_vLevData[lev]->spLevMat, *m_vLevData[lev]->u, GridLevel(lev, GridLevel::LEVEL, true));
			}
			UG_CATCH_THROW("ERROR in 'AssembledMultiGridCycle:init_linear_level_operator':"
						" Cannot init operator for level "<< lev << ".\n");

		//	remove force flag
			m_spAss->ass_tuner()->set_force_regular_grid(false);
			m_spAss->ass_tuner()->set_marker(NULL);

		//	copy the matrix into a new (smaller) one
			SmartPtr<matrix_type> mat = m_vLevData[lev]->spLevMat;
			SmartPtr<matrix_type> smoothMat = m_vLevData[lev]->spSmoothMat;

			const size_t numSmoothIndex = m_vLevData[lev]->num_smooth_indices();
			smoothMat->resize_and_clear(numSmoothIndex, numSmoothIndex);
			CopyMatrixByMapping(*smoothMat, m_vLevData[lev]->vMapGlobalToPatch, *mat);
		}
		else if((lev == m_vLevData.size() - 1) && (!m_bAdaptive))
		{
		//	in case of full refinement we simply copy the matrix (with correct numbering)
			GMG_PROFILE_BEGIN(GMG_CopySurfMat);
			SmartPtr<matrix_type> levMat = m_vLevData[lev]->spLevMat;
			ConstSmartPtr<matrix_type> surfMat = m_spSurfaceMat;

			//	loop all mapped indices
			levMat->resize_and_clear(surfMat->num_rows(), surfMat->num_cols());
			for(size_t surfFrom = 0; surfFrom < m_vSurfToLevelMap.size(); ++surfFrom)
			{
			//	get mapped level index
				UG_ASSERT(m_vSurfToLevelMap[surfFrom].level == m_topLev,
				          "All surface Indices must be on top level for full-ref.")
				const size_t lvlFrom = m_vSurfToLevelMap[surfFrom].index;

			//	loop all connections of the surface dof to other surface dofs
			//	and copy the matrix coupling into the level matrix
				typedef typename matrix_type::const_row_iterator const_row_iterator;
				const_row_iterator conn = surfMat->begin_row(surfFrom);
				const_row_iterator connEnd = surfMat->end_row(surfFrom);
				for( ;conn != connEnd; ++conn){
				//	get corresponding level connection index
					size_t lvlTo = m_vSurfToLevelMap[conn.index()].index;

				//	copy connection to level matrix
					(*levMat)(lvlFrom, lvlTo) = conn.value();
				}
			}

			#ifdef UG_PARALLEL
			levMat->set_storage_type(surfMat->get_storage_mask());
			#endif

			GMG_PROFILE_END();
			continue;
		}
	//	if no ghosts are present we can simply use the whole grid. If the base
	//	solver is carried out in serial (gathering to some processes), we have
	//	to assemble the assemble the coarse grid matrix on the whole grid as
	//	well
		if(!m_vLevData[lev]->has_ghosts() ||
			(((int)lev == m_baseLev) && (m_bBaseParallel == false)))
		{
		//	init level operator
			m_spAss->ass_tuner()->set_force_regular_grid(true);
			try{
			m_spAss->assemble_jacobian(*m_vLevData[lev]->spLevMat, *m_vLevData[lev]->u, GridLevel(lev, GridLevel::LEVEL, true));
			}
			UG_CATCH_THROW("ERROR in 'AssembledMultiGridCycle:init_linear_level_operator':"
						" Cannot init operator for level "<< lev << ".\n");
			m_spAss->ass_tuner()->set_force_regular_grid(false);
		}
	//	else we can forget about the whole-level matrix, since the needed
	//	smoothing matrix is stored in SmoothMat
		else
		{
			m_vLevData[lev]->spLevMat->resize_and_clear(0,0);
		}

		GMG_PROFILE_END();
	}

//	write computed level matrices for debug purpose
	for(size_t lev = m_baseLev; lev < m_vLevData.size(); ++lev){
		if(!m_vLevData[lev]->has_ghosts())
			write_level_debug(*m_vLevData[lev]->spLevMat, "LevelMatrix", lev);
		else
			write_smooth_level_debug(*m_vLevData[lev]->spSmoothMat, "LevelMatrix", lev);
	}

//	assemble missing coarse grid matrix contribution (only in adaptive case)
	try{
		if(m_bAdaptive)
			init_missing_coarse_grid_coupling(m_pSurfaceSol);
	}
	UG_CATCH_THROW("AssembledMultiGridCycle:init: Cannot init "
					"missing coarse grid coupling.");

	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop init_linear_level_operator\n");
}

template <typename TDomain, typename TAlgebra>
void AssembledMultiGridCycle<TDomain, TAlgebra>::
init_missing_coarse_grid_coupling(const vector_type* u)
{
	PROFILE_FUNC_GROUP("gmg");
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-start - init_missing_coarse_grid_coupling " << "\n");
//	clear matrices
	for(size_t lev = 0; lev < m_vLevData.size(); ++lev)
		m_vLevData[lev]->CoarseGridContribution.resize_and_clear(0,0);

//	if the grid is fully refined, nothing to do
	if(!m_bAdaptive){
		UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop - init_missing_coarse_grid_coupling (non-adaptive)" << "\n");
		return;
	}

//	get the surface view
	const SurfaceView& surfView = *m_spApproxSpace->surface_view();

	std::vector<ConstSmartPtr<DoFDistribution> > vLevelDD(m_vLevData.size());

//	create storage for matrices on the grid levels
	for(size_t lev = 0; lev < m_vLevData.size(); ++lev)
	{
	//	get dof distributions on levels
		ConstSmartPtr<DoFDistribution> dd
							= m_spApproxSpace->dof_distribution(GridLevel(lev, GridLevel::LEVEL, true));

		vLevelDD[lev] = dd;

	//	resize the matrix
		m_vLevData[lev]->CoarseGridContribution.resize_and_clear(dd->num_indices(),
		                                               dd->num_indices());
	}

//	surface dof distribution
	ConstSmartPtr<DoFDistribution> surfDD =
								m_spApproxSpace->dof_distribution(GridLevel(m_surfaceLev, GridLevel::SURFACE));

//	create mappings
	std::vector<std::vector<int> > vSurfLevelMapping;

	CreateSurfaceToLevelMapping(vSurfLevelMapping, vLevelDD, surfDD, surfView);

//	loop all levels to compute the missing contribution
	BoolMarker sel(*m_spApproxSpace->domain()->grid());
	for(size_t lev = 0; lev < m_vLevData.size(); ++lev)
	{
	//	select all elements, that have a shadow as a subelement, but are not itself
	//	a shadow
		sel.clear();
		SelectNonShadowsAdjacentToShadowsOnLevel(sel, surfView, lev);

	//	now set this selector to the assembling, such that only those elements
	//	will be assembled
		m_spAss->ass_tuner()->set_marker(&sel);

	//	create a surface matrix
		matrix_type surfMat;

		GridLevel surfLevel(GridLevel::TOP, GridLevel::SURFACE);

	//	assemble the surface jacobian only for selected elements
		if(u)
			m_spAss->assemble_jacobian(surfMat, *u, surfLevel);
		else
		{
		//	\todo: not use tmp vector here
			vector_type tmpVec; tmpVec.resize(m_spApproxSpace->dof_distribution(GridLevel(m_surfaceLev, GridLevel::SURFACE))->num_indices());
			m_spAss->assemble_jacobian(surfMat, tmpVec, surfLevel);
		}

	//	write matrix for debug purpose
		std::stringstream ss; ss << "MissingSurfMat_" << lev;
		write_surface_debug(surfMat, ss.str().c_str());

	//	remove the selector from the assembling procedure
		m_spAss->ass_tuner()->set_marker(NULL);

	//	project
		try{
			CopyMatrixSurfaceToLevel(m_vLevData[lev]->CoarseGridContribution,
		                             vSurfLevelMapping[lev],
		                             surfMat);
		}
		UG_CATCH_THROW("AssembledMultiGridCycle::init_missing_coarse_grid_coupling: "
					"Projection of matrix from surface to level failed.");
	}

//	write matrix for debug purpose
	for(size_t lev = 0; lev < m_vLevData.size(); ++lev)
		write_level_debug(m_vLevData[lev]->CoarseGridContribution, "MissingLevelMat", lev);

	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop - init_missing_coarse_grid_coupling " << "\n");
}

template <typename TDomain, typename TAlgebra>
void AssembledMultiGridCycle<TDomain, TAlgebra>::
init_transfer()
{
	PROFILE_FUNC_GROUP("gmg");
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-start init_transfer\n");

//	loop all levels
	for(size_t lev = m_baseLev+1; lev < m_vLevData.size(); ++lev)
	{
	//	check if same operator for prolongation and restriction used
		bool bOneOperator = false;
		if(m_vLevData[lev]->Prolongation.get() ==  m_vLevData[lev]->Restriction.get())
			bOneOperator = true;

	//	set levels
		m_vLevData[lev]->Prolongation->set_levels(GridLevel(lev-1, GridLevel::LEVEL, true),
		                                          GridLevel(lev, GridLevel::LEVEL, true));
		if(!bOneOperator)
			m_vLevData[lev]->Restriction->set_levels(GridLevel(lev-1, GridLevel::LEVEL, true),
			                                         GridLevel(lev, GridLevel::LEVEL, true));

	//	add all dirichlet post processes
		m_vLevData[lev]->Prolongation->clear_constraints();
		for(size_t i = 0; i < m_spAss->num_constraints(); ++i){
			SmartPtr<IConstraint<TAlgebra> > pp = m_spAss->constraint(i);
			m_vLevData[lev]->Prolongation->add_constraint(pp);
		}

		if(!bOneOperator){
			m_vLevData[lev]->Restriction->clear_constraints();
			for(size_t i = 0; i < m_spAss->num_constraints(); ++i){
				SmartPtr<IConstraint<TAlgebra> > pp = m_spAss->constraint(i);
				m_vLevData[lev]->Restriction->add_constraint(pp);
			}
		}

	//	init prolongation
		m_vLevData[lev]->Prolongation->init();
		if(!bOneOperator) m_vLevData[lev]->Restriction->init();

		for(size_t i = 0; i < m_vLevData[lev]->vProlongationPP.size(); ++i)
		{
			m_vLevData[lev]->vProlongationPP[i]->set_levels(GridLevel(lev, GridLevel::LEVEL, true));
			m_vLevData[lev]->vProlongationPP[i]->init();
		}

		for(size_t i = 0; i < m_vLevData[lev]->vRestrictionPP.size(); ++i)
		{
			m_vLevData[lev]->vRestrictionPP[i]->set_levels(GridLevel(lev-1, GridLevel::LEVEL, true));
			m_vLevData[lev]->vRestrictionPP[i]->init();
		}
	}

	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop init_transfer\n");
}

template <typename TDomain, typename TAlgebra>
void AssembledMultiGridCycle<TDomain, TAlgebra>::
init_projection()
{
	PROFILE_FUNC_GROUP("gmg");
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-start init_projection\n");

//	loop all levels
	for(size_t lev = m_baseLev+1; lev < m_vLevData.size(); ++lev)
	{
	//	set levels
		m_vLevData[lev]->Projection->set_levels(GridLevel(lev-1, GridLevel::LEVEL, true),
		                                        GridLevel(lev, GridLevel::LEVEL, true));

	//	init projection
		m_vLevData[lev]->Projection->init();
	}

	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop init_projection\n");
}

template <typename TDomain, typename TAlgebra>
void AssembledMultiGridCycle<TDomain, TAlgebra>::
init_smoother()
{
	PROFILE_FUNC_GROUP("gmg");
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-start init_smoother\n");

// 	Init smoother
	for(size_t lev = m_baseLev; lev < m_vLevData.size(); ++lev)
	{
	//	skip void level
		if(m_vLevData[lev]->num_indices() == 0) continue;

	//	get smooth matrix and vector
		vector_type& u = m_vLevData[lev]->get_smooth_solution();
		SmartPtr<MatrixOperator<matrix_type, vector_type> > spSmoothMat =
				m_vLevData[lev]->get_smooth_mat();

		UG_DLOG(LIB_DISC_MULTIGRID, 4, "  init_smoother: initializing pre-smoother\n");
		if(!m_vLevData[lev]->PreSmoother->init(spSmoothMat, u))
			UG_THROW("AssembledMultiGridCycle::init: Cannot init pre-smoother for level "<<lev);

		UG_DLOG(LIB_DISC_MULTIGRID, 4, "  init_smoother: initializing post-smoother\n");
		if(m_vLevData[lev]->PreSmoother.get() != m_vLevData[lev]->PostSmoother.get())
		{
			if(!m_vLevData[lev]->PostSmoother->init(spSmoothMat, u))
				UG_THROW("AssembledMultiGridCycle::init: Cannot init post-smoother for level "<<lev);
		}
	}

	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop init_smoother\n");
}

template <typename TDomain, typename TAlgebra>
void AssembledMultiGridCycle<TDomain, TAlgebra>::
init_base_solver()
{
	PROFILE_FUNC_GROUP("gmg");
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-start init_base_solver\n");
//	skip void level
	if(m_vLevData[m_baseLev]->num_indices() == 0) return;

#ifdef UG_PARALLEL
//	check, if a gathering base solver is required:
	if(!m_bBaseParallel)
	{
	//	check if gathering base solver possible: If some horizontal layouts are
	//	given, we know, that still the grid is distributed. But, if no
	//	vertical layouts are present in addition, we can not gather the vectors
	//	to on proc. Write a warning an switch to distributed coarse solver
		vector_type& d = *m_vLevData[m_baseLev]->d;
	//	the base-solver only operates on normal and vertical-master elements...
		if(d.layouts()->vertical_slave().empty()){
			if((!d.layouts()->master().empty() || !d.layouts()->slave().empty()) &&
			   (d.layouts()->vertical_slave().empty() && d.layouts()->vertical_master().empty()))
			{
			//todo	add a check whether the base-solver supports parallel execution
			//		and also make sure, that all processes change to parallel execution.
			//		as soon as this is done, revert UG_THROW to UG_LOG and set m_bBaseParallel
			//		to true, if the solver supports this...
				UG_THROW("ERROR in 'AssembledMultiGridCycle::init_base_solver': "
						" Cannot init distributed base solver on level "<< m_baseLev << ":\n"
						" Base level distributed among processes and no possibility"
						" of gathering (vert. interfaces) present. But a gathering"
						" solving is required. Choose gmg:set_parallel_base_solver(true)"
						" to avoid this error.\n");
			//m_bBaseParallel = true;
			}
			else
			{
			//	we init the base solver with the whole grid matrix
				if(!m_spBaseSolver->init(m_vLevData[m_baseLev]->spLevMat, *m_vLevData[m_baseLev]->u))
					UG_THROW("AssembledMultiGridCycle::init: Cannot init base "
							"solver on baselevel "<< m_baseLev);
			}
		}
	//todo:	it could make sense to communicate here, to make sure that all processes
	//		do the same thing and that exactly one is executing the serial base solver...
	}

//	\todo: add a check if base solver can be run in parallel. This needs to
//		   introduce such a flag in the solver.
//	in Serial or in case of a distributed coarse grid solver, we can simply use
//	the smoothing matrices to set up the solver.
	if(m_bBaseParallel)
#endif
	{
	//	get smooth matrix and vector
		vector_type& u = m_vLevData[m_baseLev]->get_smooth_solution();
		SmartPtr<MatrixOperator<matrix_type, vector_type> > spSmoothMat =
				m_vLevData[m_baseLev]->get_smooth_mat();

		//write_level_debug(*spSmoothMat, "GMG_BaseSolver_Matrix", m_baseLev);

		if(!m_spBaseSolver->init(spSmoothMat, u))
			UG_THROW("AssembledMultiGridCycle::init: Cannot init base solver "
					"on baselevel "<< m_baseLev);
	}

	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop init_base_solver\n");
}

template <typename TDomain, typename TAlgebra>
template <typename TElem>
void AssembledMultiGridCycle<TDomain, TAlgebra>::
init_surface_to_level_mapping()
{
/*	This Method is used to create a caching the transfer between surface and
 * 	level grid functions. Note, that in the adaptive case the surface grid is
 * 	distributed over several levels (all elements that do not have children). But
 * 	even in the full refinement case the surface level and the top level may
 * 	not have the same index pattern, since a different sorting may be used (however
 * 	the number of indices must be equal in the full-ref case).
 * 	In every case, each surface index has a corresponding index on some level
 * 	of the level grid functions. In this method this index is looked up and
 * 	stored in a vector (for fast access). I.e. for every surface index i, the
 * 	corresponding index in the level grid function is stored in m_vSurfToLevelMap[i]
 * 	together with the level of the grid function. Using this mapping copying
 * 	between surface <-> levels is implemented. Note: When Shadow-Copy are present
 * 	the dof manager usually assigns the same surface index to both, shadowing and
 * 	shadow-copy. Thus, there exist more than one corresponding level index for
 * 	such a surface index. In this case the shadowing index is used in the mapping
 * 	since this index will have the correct value at the end of the multigrid
 * 	cycle and at startup the projection to the level is necessary only to shadowing,
 * 	because shadows will get their value by transfer between the level. In order
 * 	to get the map this way, the surface loop below is only performed on
 * 	SURFACE_NONCOPY elements.
 */

	ConstSmartPtr<SurfaceView> spSurfView = m_spApproxSpace->surface_view();
	std::vector<ConstSmartPtr<DoFDistribution> > vLevelDD(num_levels());
	for(size_t lev = 0; lev < num_levels(); ++lev)
		vLevelDD[lev] = m_spApproxSpace->dof_distribution(GridLevel(lev, GridLevel::LEVEL, true));
	ConstSmartPtr<DoFDistribution> surfDD = m_spApproxSpace->dof_distribution(GridLevel(m_surfaceLev, GridLevel::SURFACE));

//	iterators for subset
	// \todo: The loop below should only be on SURFACE_NONCOPY, since the
	//		 copy-shadows have the same indices as their shadowing. In such a
	//		 case the child index (shadowing) must win - this is only ensured by
	//		 the details of the implementation of the iterator - looping from base level
	//		 up through the multigrid. However, this could be realized by
	//		 looping only non-copy elements. But it seems, that on a process
	//		 the shadow-copy may exist without its shadowing. In such a case
	//		 the mapping is invalid. To fix this, the loop is extended temporarily
	//		 below.
	typedef typename DoFDistribution::traits<TElem>::const_iterator iter_type;
	iter_type iter = surfDD->begin<TElem>(SurfaceView::ALL);
	iter_type iterEnd = surfDD->end<TElem>(SurfaceView::ALL);

//	vector of indices
	std::vector<size_t> vSurfInd, vLevelInd;

//	loop all elements of type
	for( ; iter != iterEnd; ++iter){
	//	get elem and its level
		TElem* elem = *iter;
		const int level = spSurfView->get_level(elem);

	//	extract all algebra indices for the element on surface and level
		surfDD->inner_algebra_indices(elem, vSurfInd);
		vLevelDD[level]->inner_algebra_indices(elem, vLevelInd);
		UG_ASSERT(vSurfInd.size() == vLevelInd.size(), "Number of indices does not match.");

	//	set mapping index
		for(size_t i = 0; i < vSurfInd.size(); ++i){
			m_vSurfToLevelMap[vSurfInd[i]] = LevelIndex(vLevelInd[i], level);
		}
	}
}


template <typename TDomain, typename TAlgebra>
void AssembledMultiGridCycle<TDomain, TAlgebra>::
init_surface_to_level_mapping()
{
	ConstSmartPtr<DoFDistribution> surfDD = m_spApproxSpace->dof_distribution(GridLevel(m_surfaceLev, GridLevel::SURFACE));

	m_vSurfToLevelMap.resize(0);
	m_vSurfToLevelMap.resize(surfDD->num_indices());

	if(surfDD->max_dofs(VERTEX)) init_surface_to_level_mapping<VertexBase>();
	if(surfDD->max_dofs(EDGE))   init_surface_to_level_mapping<EdgeBase>();
	if(surfDD->max_dofs(FACE))   init_surface_to_level_mapping<Face>();
	if(surfDD->max_dofs(VOLUME)) init_surface_to_level_mapping<Volume>();
}


////////////////////////////////////////////////////////////////////////////////
// Init Level Data
////////////////////////////////////////////////////////////////////////////////

template <typename TDomain, typename TAlgebra>
void AssembledMultiGridCycle<TDomain, TAlgebra>::
top_level_required(size_t topLevel)
{
	PROFILE_FUNC_GROUP("gmg");

//	allocated level if needed
	while(num_levels() <= topLevel){
		m_vLevData.push_back(SmartPtr<LevData>(new LevData));
	}

//	free level if needed
	while(num_levels() > topLevel+1){
		m_vLevData.pop_back();
	}

//	reinit all levels
	m_NonGhostMarker.clear();
	for(size_t lev = m_baseLev; lev < m_vLevData.size(); ++lev)
	{
		m_vLevData[lev]->update(lev,
		                       m_spApproxSpace->dof_distribution(GridLevel(lev, GridLevel::LEVEL, true)),
		                       m_spApproxSpace,
		                       m_spAss,
		                       *m_spPreSmootherPrototype,
		                       *m_spPostSmootherPrototype,
		                       *m_spProjectionPrototype,
		                       *m_spProlongationPrototype,
		                       *m_spRestrictionPrototype,
		                       m_vspProlongationPostProcess,
		                       m_vspRestrictionPostProcess,
		                       m_NonGhostMarker);
	}
}


template <typename TDomain, typename TAlgebra>
AssembledMultiGridCycle<TDomain, TAlgebra>::
LevData::~LevData()
{}

template <typename TDomain, typename TAlgebra>
AssembledMultiGridCycle<TDomain, TAlgebra>::
LevData::
LevData()
: spLevDD(0),
  m_spApproxSpace(0),
  spLevMat(new MatrixOperator<matrix_type, vector_type>),
  spSmoothMat(new MatrixOperator<matrix_type, vector_type>),
  PreSmoother(0), PostSmoother(0),
  Projection(0), Prolongation(0), Restriction(0),
  vProlongationPP(0), vRestrictionPP(0),
  u(0), c(0), d(0), t(0),
  su(0), sc(0), sd(0), st(0)
#ifdef UG_PARALLEL
  ,spSmoothLayouts(0)
#endif
{};


template <typename TDomain, typename TAlgebra>
void
AssembledMultiGridCycle<TDomain, TAlgebra>::
LevData::
update(size_t lev,
       SmartPtr<DoFDistribution> levDD,
       SmartPtr<ApproximationSpace<TDomain> > approxSpace,
       SmartPtr<IAssemble<TAlgebra> > spAss,
       ILinearIterator<vector_type>& presmoother,
       ILinearIterator<vector_type>& postsmoother,
       ITransferOperator<TAlgebra>& projection,
       ITransferOperator<TAlgebra>& prolongation,
       ITransferOperator<TAlgebra>& restriction,
       std::vector<SmartPtr<ITransferPostProcess<TAlgebra> > >& vprolongationPP,
       std::vector<SmartPtr<ITransferPostProcess<TAlgebra> > >& vrestrictionPP,
       BoolMarker& nonGhostMarker)
{
	PROFILE_FUNC_GROUP("gmg");
//	get dof distribution
	spLevDD = levDD;
	m_spApproxSpace = approxSpace;

//	resize vectors for operations on whole grid level
	if(u.invalid()) u = SmartPtr<GridFunction<TDomain, TAlgebra> >(new GridFunction<TDomain, TAlgebra>(approxSpace, spLevDD, false));
	if(c.invalid()) c = SmartPtr<GridFunction<TDomain, TAlgebra> >(new GridFunction<TDomain, TAlgebra>(approxSpace, spLevDD, false));
	if(d.invalid()) d = SmartPtr<GridFunction<TDomain, TAlgebra> >(new GridFunction<TDomain, TAlgebra>(approxSpace, spLevDD, false));
	if(t.invalid()) t = SmartPtr<GridFunction<TDomain, TAlgebra> >(new GridFunction<TDomain, TAlgebra>(approxSpace, spLevDD, false));

//	enlarge the vectors on the number of the extra dofs
//todo: Remove it here and implement the management of the extra dofs in the
//		DoFDistribution class
	const size_t numIndex = spLevDD->num_indices();
	u->resize(numIndex);
	c->resize(numIndex);
	d->resize(numIndex);
	t->resize(numIndex);

//	prepare level operator
#ifdef UG_PARALLEL
//	default storage types for c and d to avoid incompatible types.
	c->set_storage_type(PST_CONSISTENT);
	d->set_storage_type(PST_ADDITIVE);
	spLevMat->set_layouts(spLevDD->layouts());
#endif

//	post smoother only created if not the same operator
	if(PreSmoother.invalid()) PreSmoother = presmoother.clone();
	if(&postsmoother == &presmoother) PostSmoother = PreSmoother;
	else{if(PostSmoother.invalid()) PostSmoother = postsmoother.clone();}

	if(Projection.invalid()) Projection = projection.clone();

//	restriction only created if not the same operator
	if(Prolongation.invalid()) Prolongation = prolongation.clone();
	if(&prolongation == &restriction)	Restriction = Prolongation;
	else{if(Restriction.invalid()) Restriction = restriction.clone();}

	vProlongationPP.clear();
	for(size_t i = 0; i < vprolongationPP.size(); ++i)
			vProlongationPP.push_back(vprolongationPP[i]->clone());

	vRestrictionPP.clear();
	for(size_t i = 0; i < vrestrictionPP.size(); ++i)
			vRestrictionPP.push_back(vrestrictionPP[i]->clone());

	GridLevel gl = GridLevel(lev, GridLevel::LEVEL, false);
	su = SmartPtr<GridFunction<TDomain, TAlgebra> >(new GridFunction<TDomain, TAlgebra>(approxSpace, gl, false));
	sc = SmartPtr<GridFunction<TDomain, TAlgebra> >(new GridFunction<TDomain, TAlgebra>(approxSpace, gl, false));
	sd = SmartPtr<GridFunction<TDomain, TAlgebra> >(new GridFunction<TDomain, TAlgebra>(approxSpace, gl, false));
	st = SmartPtr<GridFunction<TDomain, TAlgebra> >(new GridFunction<TDomain, TAlgebra>(approxSpace, gl, false));

//	IN PARALLEL:
//	In the parallel case one may have vertical slaves/masters. Those are needed
//	when the grid hierarchy ends at a certain point on one process but is still
//	continued on another. In this case the values are transfered from the
//	valishing processor and copied to another. In addition it may happen, that
//	at a certain level a process only starts to have a grid elements and the
//	values are copied from another process to this process.
//	While these parts of the grid is needed for the transfer of correction or
//	defect between the grid levels, on those elements no smoothing is performed.
//	Therefore, if vertical masters are present only on the part of the grid
//	without vertical masters is smoothed. To account for this, in addition
//	smoothing vectors and matrices of smaller size are created and assembled.
//	Please note that smoothing is performed on vertical slaves.
#ifdef UG_PARALLEL
//	if no vertical masters, there can be no ghost and we're ready. By ghosts
//	we denote vertical masters, that are not horizontal master/slave
	if(spLevDD->layouts()->vertical_master().empty())
	{
		m_numSmoothIndices = numIndex;
		return;
	}

//	If ghosts are present we create the infrastructure for this. This includes
//	the creation of Smoother on a smaller patch and required vectors/matrices
//	Also a mapping between the index set on the whole grid and the index set
//	on the smoothing patch must be created

//	** 1. **: We create the mapping between the index sets
//	create a vector of size of the whole grid with 1 everywhere
	vMapGlobalToPatch.clear(); vMapGlobalToPatch.resize(numIndex, 1);

//	set the vector to -1 where vertical masters are present, the set all
//	indices back to 1 where the index is also a horizontal master/slave
	SetLayoutValues(&vMapGlobalToPatch, spLevDD->layouts()->vertical_master(), -1);
	SetLayoutValues(&vMapGlobalToPatch, spLevDD->layouts()->master(), 1);
	SetLayoutValues(&vMapGlobalToPatch, spLevDD->layouts()->slave(), 1);

//	now we create the two mapping:
//	vMapGlobalToPatch: mapping (whole grid index -> patch index): the non-ghost indices
//	are mapped to a patch index, while the ghosts are flagged by a -1 index
//	vMapPatchToGlobal: mapping (patch index -> whole grid index): For each patch index the
//	corresponding whole grid index is stored
	vMapPatchToGlobal.clear();
	for(size_t j = 0; j < vMapGlobalToPatch.size(); ++j)
	{
	//	if the index is still negative (i.e. ghost, leave index at -1)
		if(vMapGlobalToPatch[j] == -1) continue;

	//	if the index is a non-ghost set the new index
		vMapGlobalToPatch[j] = vMapPatchToGlobal.size();

	//	since in the patch we store the mapping index
		vMapPatchToGlobal.push_back(j);
	}

//	now we know the size of the smoothing patch index set and resize help vectors
//	by the preceeding 's' the relation to the smoothing is indicated
	const size_t numSmoothIndex = vMapPatchToGlobal.size();
	m_numSmoothIndices = numSmoothIndex;

	// \todo: This is not correct here: DoFIndices on elements on smooth patch are still
	//		  as in level numbering, but vector size must be restricted. It is
	//		  necessary to add a different DoFDistribution for this type of
	//		  dofs. In order to keep current (algebraic) solvers running, this
	//		  resize is still performed at this point.
	su->resize(numSmoothIndex);
	sc->resize(numSmoothIndex);
	sd->resize(numSmoothIndex);
	st->resize(numSmoothIndex);

	if(numSmoothIndex == 0){
	//	set default storage types to avoid incompatibilities
		sc->set_storage_type(PST_CONSISTENT);
		sd->set_storage_type(PST_ADDITIVE);
	}

//	** 2. **: We have to create new layouts for the smoothers since on the
//	smoothing patch the indices are labeled differently.
//	copy layouts
	spSmoothLayouts = SmartPtr<AlgebraLayouts>(new AlgebraLayouts);
	spSmoothLayouts->master() = spLevDD->layouts()->master();
	spSmoothLayouts->slave() = spLevDD->layouts()->slave();
	spSmoothLayouts->comm() = spLevDD->layouts()->comm();
	spSmoothLayouts->proc_comm() = spLevDD->layouts()->proc_comm();

//	Replace indices in the layout with the smaller (smoothing patch) indices
	ReplaceIndicesInLayout(spSmoothLayouts->master(), vMapGlobalToPatch);
	ReplaceIndicesInLayout(spSmoothLayouts->slave(), vMapGlobalToPatch);

//	replace old layouts by new modified ones
	sc->set_layouts(spSmoothLayouts);
	su->set_layouts(spSmoothLayouts);
	sd->set_layouts(spSmoothLayouts);
	st->set_layouts(spSmoothLayouts);

//	set the layouts in the smooth matrix
	spSmoothMat->set_layouts(spSmoothLayouts);

//	** 3. **: Since smoothing is only performed on non-ghost elements, the
//	corresoding operator must be assembled only on those elements. So we
//	use a selector to mark all non-ghosts and assemble on those later
//	get distributed Grid manager
	DistributedGridManager* pDstGrdMgr
		= m_spApproxSpace->domain()->distributed_grid_manager();

//	select all ghost geometric objects
	for(int si = 0; si < spLevDD->num_subsets(); ++si)
	{
		SelectNonGhosts<VertexBase>(nonGhostMarker, *pDstGrdMgr,
								 spLevDD->template begin<VertexBase>(si),
								 spLevDD->template end<VertexBase>(si));
		SelectNonGhosts<EdgeBase>(nonGhostMarker, *pDstGrdMgr,
								 spLevDD->template begin<EdgeBase>(si),
								 spLevDD->template end<EdgeBase>(si));
		SelectNonGhosts<Face>(nonGhostMarker, *pDstGrdMgr,
								 spLevDD->template begin<Face>(si),
								 spLevDD->template end<Face>(si));
		SelectNonGhosts<Volume>(nonGhostMarker, *pDstGrdMgr,
								 spLevDD->template begin<Volume>(si),
								 spLevDD->template end<Volume>(si));
	}

#else //PARALLEL

//	We have to smooth on the entire level
	m_numSmoothIndices = numIndex;

#endif
}

////////////////////////////////////////////////////////////////////////////////
// Cycle - Methods
////////////////////////////////////////////////////////////////////////////////

// perform the smoothing
template <typename TDomain, typename TAlgebra>
void AssembledMultiGridCycle<TDomain, TAlgebra>::
smooth(vector_type& c, vector_type& d, vector_type& tmp,
       MatrixOperator<matrix_type, vector_type>& A,
       ILinearIterator<vector_type>& S,
       size_t lev, int nu)
{
	PROFILE_FUNC_GROUP("gmg");
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-start - smooth on level " << lev << "\n");

	if(d.size() == 0){
	//	since no parallel communication takes place in this method, we may
	//	return immediately, if d is empty.
		return;
	}

// 	smooth nu times
	for(int i = 0; i < nu; ++i)
	{
	//	switch if adaptive case must be handled
		if(!m_bAdaptive)
		{
		// 	Compute Correction of one smoothing step:
		//	a)  Compute t = B*d with some iterator B
		//	b) 	Update Defect d := d - A * t
			if(!S.apply_update_defect(tmp, d))
				UG_THROW("AssembledMultiGridCycle::smooth: Smoothing step "
						<< i+1 << " on level " << lev << " failed.");

		// 	add correction of smoothing step to level correction
		//	(Note: we do not work on c directly here, since we update the defect
		//	       after every smoothing step. The summed up correction corresponds
		//		   to the total correction of the whole smoothing.)
			c += tmp;
		}
		else
	//	This is the adaptive case. Here, we must ensure, that the added correction
	//	is zero on the adaptively refined patch boundary of this level
		{
		// 	Compute Correction of one smoothing step, but do not update defect
		//	a)  Compute t = B*d with some iterator B

			if(!S.apply(tmp, d))
				UG_THROW("AssembledMultiGridCycle::smooth: Smoothing step "
						<< i+1 << " on level " << lev << " failed.");

		//	get surface view
			const SurfaceView& surfView = *m_spApproxSpace->surface_view();

			//write_level_debug(tmp, "GMG_AdaptCorBeforeSetZero", lev);

		//	First we reset the correction to zero on the patch boundary.
			if(m_vLevData[lev]->has_ghosts())
				SetZeroOnShadowing(tmp, m_vLevData[lev]->spLevDD, surfView,
								   &m_vLevData[lev]->vMapGlobalToPatch);
			else
				SetZeroOnShadowing(tmp, m_vLevData[lev]->spLevDD, surfView);

			//write_level_debug(tmp, "GMG_AdaptCorAfterSetZero", lev);

		//	now, we can update the defect with this correction ...
			A.apply_sub(d, tmp);

		//	... and add the correction to to overall correction
			c += tmp;
		}
	}

	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop - smooth on level " << lev << "\n");
}

template <typename TDomain, typename TAlgebra>
void AssembledMultiGridCycle<TDomain, TAlgebra>::
presmooth(size_t lev)
{
	PROFILE_FUNC_GROUP("gmg");
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-start - presmooth on level " << lev << "\n");
//	Get all needed vectors and operators

//	get vectors used in smoothing operations. (This is needed if vertical
//	masters are present, since no smoothing is performed on those. In that case
//	only on a smaller part of the grid level - the smoothing patch - the
//	smoothing is performed)
	vector_type& sd = m_vLevData[lev]->get_smooth_defect();
	vector_type& sc = m_vLevData[lev]->get_smooth_correction();
	vector_type& sTmp = m_vLevData[lev]->get_smooth_tmp();

//	get smoother on this level and corresponding operator
	SmartPtr<MatrixOperator<matrix_type, vector_type> > spSmoothMat =
		m_vLevData[lev]->get_smooth_mat();

// 	reset correction to zero on this level
	sc.set(0.0);

//	We start the multi grid cycle on this level by smoothing the defect. This
//	means that we compute a correction c, such that the defect is "smoother".
//	If ghosts are present in parallel, we only smooth on a patch. Thus we first
//	copy the values from the whole grid level to the smoothing patch.
	m_vLevData[lev]->copy_defect_to_smooth_patch();

	write_smooth_level_debug(m_vLevData[lev]->get_smooth_defect(), "GMG_Def_BeforePreSmooth", lev);
	write_smooth_level_debug(m_vLevData[lev]->get_smooth_correction(), "GMG_Cor_BeforePreSmooth", lev);

// 	pre-smoothing
	GMG_PROFILE_BEGIN(GMG_PreSmooth);
	try{
		smooth(sc, sd, sTmp, *spSmoothMat, *m_vLevData[lev]->PreSmoother, lev, m_numPreSmooth);
	}
	UG_CATCH_THROW("AssembledMultiGridCycle::lmgc: Pre-Smoothing on "
					"level " << lev << " failed. ");
	GMG_PROFILE_END();

	write_smooth_level_debug(m_vLevData[lev]->get_smooth_defect(), "GMG_Def_AfterPreSmooth", lev);
	write_smooth_level_debug(m_vLevData[lev]->get_smooth_correction(), "GMG_Cor_AfterPreSmooth", lev);

//	now copy the values of d back to the whole grid, since the restriction
//	acts on the whole grid. Since we will perform an addition of the vertical
//	slaves to the vertical masters, we will thereby set the values on ghosts nodes
//	to zero.
	m_vLevData[lev]->copy_defect_from_smooth_patch(true);

//NOTE: Since we do not copy the correction back from the smooth patch, the resulting
//		correction will be zero in all entries, if ghosts were present on a process.
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop - presmooth on level " << lev << "\n");
}


template <typename TDomain, typename TAlgebra>
void AssembledMultiGridCycle<TDomain, TAlgebra>::
restriction(size_t lev)
{
	PROFILE_FUNC_GROUP("gmg");
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-start - restriction on level " << lev << "\n");
//	Get all needed vectors and operators

//	Get vectors defined on whole grid (including ghosts) on this level
//	denote by: c = Correction, d = Defect, tmp = Help vector
	vector_type& d = *m_vLevData[lev]->d;

//	Lets get a reference to the coarser level correction, defect, help vector
	UG_ASSERT(lev > 0, "restriction can't be applied on level 0.");
	vector_type& cd = *m_vLevData[lev-1]->d;

//	## PARALLEL CASE: gather vertical
	#ifdef UG_PARALLEL
		write_level_debug(d, "GMG__TestBeforeGather", lev);
	//	Send vertical slave values to master.
	//	we have to make sure that d is additive after this operation and that it
	//	is additive-unique regarding v-masters and v-slaves (v-slaves will be set to 0)
		if(d.size() > 0){
			gather_vertical(d);
			SetLayoutValues(&d, d.layouts()->vertical_slave(), 0);
		}

		write_level_debug(d, "GMG__TestAfterGather", lev);
	#endif



//	Now we can restrict the defect from the fine level to the coarser level.
//	This is done using the transposed prolongation.
	if((cd.size() > 0) && (d.size() > 0)){
		GMG_PROFILE_BEGIN(GMG_RestrictDefect);
		try{
			m_vLevData[lev]->Restriction->do_restrict(cd, d);
		}
		UG_CATCH_THROW("AssembledMultiGridCycle::lmgc: Restriction of "
					"Defect from level "<<lev<<" to "<<lev-1<<" failed. ");
		GMG_PROFILE_END();

		write_level_debug(cd, "GMG_Def_RestrictedNoPP", lev-1);
	//	apply post processes
		for(size_t i = 0; i < m_vLevData[lev]->vRestrictionPP.size(); ++i)
			m_vLevData[lev]->vRestrictionPP[i]->post_process(m_vLevData[lev-1]->d);
		write_level_debug(cd, "GMG_Def_RestrictedWithPP", lev-1);
	}

	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop - restriction on level " << lev << "\n");
}

template <typename TDomain, typename TAlgebra>
void AssembledMultiGridCycle<TDomain, TAlgebra>::
prolongation(size_t lev)
{

	PROFILE_FUNC_GROUP("gmg");
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-start - prolongation on level " << lev << "\n");
//	Get all needed vectors and operators

//	Get vectors defined on whole grid (including ghosts) on this level
//	denote by: c = Correction, d = Defect, tmp = Help vector
	#ifdef UG_PARALLEL
	vector_type& d = *m_vLevData[lev]->d;
	#endif

	vector_type& tmp = *m_vLevData[lev]->t;

//	get vectors used in smoothing operations. (This is needed if vertical
//	masters are present, since no smoothing is performed on those. In that case
//	only on a smaller part of the grid level - the smoothing patch - the
//	smoothing is performed)
	vector_type& sd = m_vLevData[lev]->get_smooth_defect();
	vector_type& sc = m_vLevData[lev]->get_smooth_correction();
	vector_type& sTmp = m_vLevData[lev]->get_smooth_tmp();

//	Lets get a reference to the coarser level correction, defect, help vector
	UG_ASSERT(lev > 0, "prolongatoin can't be applied on level 0.");
	vector_type& cc = *m_vLevData[lev-1]->c;
	vector_type& cTmp = *m_vLevData[lev-1]->t;

//	get smoothing operator on this level
	SmartPtr<MatrixOperator<matrix_type, vector_type> > spSmoothMat =
		m_vLevData[lev]->get_smooth_mat();

//	## INTERPOLATE CORRECTION
	if((cc.size() > 0) && (tmp.size() > 0)){
	//	now we can interpolate the coarse grid correction from the coarse level
	//	to the fine level
		GMG_PROFILE_BEGIN(GMG_InterpolateCorr);
		try{
			m_vLevData[lev]->Prolongation->prolongate(tmp, cc);
		}
		UG_CATCH_THROW("AssembledMultiGridCycle::lmgc: Prolongation from"
					" level " << lev-1 << " to " << lev << " failed. "
					"(BaseLev="<<m_baseLev<<", TopLev="<<m_topLev<<")\n");
		GMG_PROFILE_END();

	//	apply post processes
		for(size_t i = 0; i < m_vLevData[lev]->vProlongationPP.size(); ++i)
			m_vLevData[lev]->vProlongationPP[i]->post_process(m_vLevData[lev]->t);
	}

//	PARALLEL CASE: Receive values of correction for vertical slaves
//	If there are vertical slaves/masters on the coarser level, we now copy
//	the correction values from the v-master DoFs to the v-slave	DoFs.
//	since dummies may exist, we'll copy the broadcasted correction to h-slave
//	interfaces (dummies are always h-slaves)
	#ifdef UG_PARALLEL
	broadcast_vertical(tmp);
	#endif
	write_level_debug(tmp, "GMG_Prol_CoarseGridCorr", lev);

//	## PROJECT COARSE GRID CORRECTION ONTO SMOOTH AREA
	m_vLevData[lev]->copy_tmp_to_smooth_patch();

// 	## ADD COARSE GRID CORRECTION
	GMG_PROFILE_BEGIN(GMG_AddCoarseGridCorr);
	if(sc.size() > 0){
	//	if ensures that no incompatible storage types are added in the case of
	//	empty vectors.
		sc += sTmp;
	}
	GMG_PROFILE_END(); // GMG_AddCoarseGridCorr

//	## UPDATE DEFECT FOR COARSE GRID CORRECTION
//	due to gathering during restriction, the defect is currently additive so
//	that v-masters have the whole value and v-slaves are all 0 (in d. sd may differ).
//	we thus have to transport all values back to v-slaves and have to make sure
//	that d is additive again.
	write_level_debug(*m_vLevData[lev]->d, "GMG_Prol_BeforeBroadcast", lev);
	write_smooth_level_debug(sd, "GMG_Def_Prol_BeforeBroadcastSmooth", lev);
	#ifdef UG_PARALLEL
	broadcast_vertical_add(d);
	SetLayoutValues(&d, d.layouts()->vertical_master(), 0);

	m_vLevData[lev]->copy_defect_to_smooth_patch();
	#endif
	write_smooth_level_debug(sd, "GMG_Def_Prol_BeforeUpdate", lev);

//	the correction has changed c := c + t. Thus, we also have to update
//	the defect d := d - A*t
	GMG_PROFILE_BEGIN(GMG_UpdateDefectForCGCorr);
	if(sd.size() > 0){
		spSmoothMat->apply_sub(sd, sTmp);
	}
	GMG_PROFILE_END(); // GMG_UpdateDefectForCGCorr

//	## ADAPTIVE CASE
	if(m_bAdaptive)
	{
	//todo:	coarse grid correction on smooth patch, only?
	//	in the adaptive case there is a small part of the coarse coupling that
	//	has not been used to update the defect. In order to ensure, that the
	//	defect on this level still corresponds to the updated defect, we need
	//	to add if here. This is done in three steps:
	//	a) Compute the coarse update of the defect induced by missing coupling
		cTmp.set(0.0);
		if(cc.size() > 0){
			if(!m_vLevData[lev-1]->CoarseGridContribution.apply(cTmp, cc))
				UG_THROW("AssembledMultiGridCycle::lmgc: Could not compute"
						" missing update defect contribution on level "<<lev-1);
		}

	//	cTmp is additive but we need a consistent correction
		#ifdef UG_PARALLEL
			cTmp.set_storage_type(PST_ADDITIVE);
			//cTmp.change_storage_type(PST_CONSISTENT);
		#endif

		write_level_debug(cTmp, "GMG_AdaptiveCoarseGridContribution", lev - 1);

	//	get surface view
		const SurfaceView& surfView = *m_spApproxSpace->surface_view();

	//	b) interpolate the coarse defect up
	//	since we add a consistent correction, we need a consistent defect to which
	//	we add the values.
		#ifdef UG_PARALLEL
			m_vLevData[lev]->copy_defect_from_smooth_patch();
			write_level_debug(*m_vLevData[lev]->d, "GMG_Prol_DefOnlyCoarseCorrAdditive", lev);
			//d.change_storage_type(PST_CONSISTENT);
		#endif
		write_level_debug(*m_vLevData[lev]->d, "GMG_Prol_DefOnlyCoarseCorr", lev);

		std::vector<ConstSmartPtr<DoFDistribution> > vLevelDD(num_levels());
		for(size_t l = 0; l < num_levels(); ++l)
			vLevelDD[l] = m_spApproxSpace->dof_distribution(GridLevel(l, GridLevel::LEVEL, true));

		AddProjectionOfShadows(level_defects(),
		                       vLevelDD,
							   cTmp, m_vLevData[lev-1]->spLevDD,
							   lev -1,
							   -1.0,
							   surfView);

		//	copy defect to smooth patch
		#ifdef UG_PARALLEL
			//d.change_storage_type(PST_ADDITIVE);
			m_vLevData[lev]->copy_defect_to_smooth_patch();
		#endif
		write_smooth_level_debug(m_vLevData[lev]->get_smooth_defect(), "GMG_Def_Prolongated", lev);
		//write_level_debug(cTmp, "GMG_Prol_CorrAdaptAdding", lev-1);
	}

	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop - prolongation on level " << lev << "\n");
}

template <typename TDomain, typename TAlgebra>
void AssembledMultiGridCycle<TDomain, TAlgebra>::
postsmooth(size_t lev)
{
	PROFILE_FUNC_GROUP("gmg");
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-start - postsmooth on level " << lev << "\n");
//	get vectors used in smoothing operations. (This is needed if vertical
//	masters are present, since no smoothing is performed on those. In that case
//	only on a smaller part of the grid level - the smoothing patch - the
//	smoothing is performed)
	vector_type& sd = m_vLevData[lev]->get_smooth_defect();
	vector_type& sc = m_vLevData[lev]->get_smooth_correction();
	vector_type& sTmp = m_vLevData[lev]->get_smooth_tmp();

//	get smoother on this level and corresponding operator
	SmartPtr<MatrixOperator<matrix_type, vector_type> > spSmoothMat =
		m_vLevData[lev]->get_smooth_mat();


// 	## POST-SMOOTHING
//	before we smooth, we want to make sure that sd is additive unique. This
//	results in dummies having value 0.
	//sd.change_storage_type(PST_UNIQUE);

//	We smooth the updated defect again. This means that we compute a
//	correction c, such that the defect is "smoother".
	GMG_PROFILE_BEGIN(GMG_PostSmooth);
	try{
		smooth(sc, sd, sTmp, *spSmoothMat, *m_vLevData[lev]->PostSmoother, lev, m_numPostSmooth);
	}
	UG_CATCH_THROW("AssembledMultiGridCycle::lmgc: Post-Smoothing on"
					" level " << lev << " failed. ")
	GMG_PROFILE_END();

//	## PROJECT DEFECT, CORRECTION BACK TO WHOLE GRID FOR RESTRICTION
	m_vLevData[lev]->copy_defect_from_smooth_patch();
	m_vLevData[lev]->copy_correction_from_smooth_patch();

	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop - postsmooth on level " << lev << "\n");
}

// performs the base solving
template <typename TDomain, typename TAlgebra>
void AssembledMultiGridCycle<TDomain, TAlgebra>::
base_solve(size_t lev)
{
	PROFILE_FUNC_GROUP("gmg");
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-start - base_solve on level " << lev << "\n");
	try{
//	get vectors used in smoothing operations. (This is needed if vertical
//	masters are present, since no smoothing is performed on those. In that case
//	only on a smaller part of the grid level - the smoothing patch - the
//	smoothing is performed)
	vector_type& sd = m_vLevData[lev]->get_smooth_defect();
	vector_type& sc = m_vLevData[lev]->get_smooth_correction();

//	SOLVE BASE PROBLEM
//	Here we distinguish two possibilities:
//	a) The coarse grid problem is solved in parallel, using a parallel solver
//	b) First all vectors are gathered to one process, solved on this one
//	   process and then again distributed

//	CASE a): We solve the problem in parallel (or normally for sequential code)
#ifdef UG_PARALLEL
//	vector defined on whole grid (including ghosts) on this level
	vector_type& d = *m_vLevData[lev]->d;

	if( m_bBaseParallel ||
	   (d.layouts()->vertical_slave().empty() &&
		d.layouts()->vertical_master().empty()))
	{
#endif
		UG_DLOG(LIB_DISC_MULTIGRID, 3, " GMG: entering serial basesolver branch.\n");
		if(m_vLevData[lev]->num_indices()){
		//	LIFTING c TO SOLVING AREA
			m_vLevData[lev]->copy_defect_to_smooth_patch();

			#ifdef UG_PARALLEL
				write_level_debug(d, "GMG_Def_BeforeBaseSolver", lev);
			#endif

			GMG_PROFILE_BEGIN(GMG_BaseSolver);
			sc.set(0.0);
			try{
				if(!m_spBaseSolver->apply(sc, sd))
					UG_THROW("AssembledMultiGridCycle::lmgc: Base solver on base "
							"level " << lev << " failed. (BaseLev="<<m_baseLev<<
							", TopLev="<<m_topLev<<")");
			}
			UG_CATCH_THROW("GMG: BaseSolver::apply failed. (case: a).")

		//	*) if baseLevel == surfaceLevel, we need also need the updated defect
		//	*) if adaptive case, we also need to update the defect, such that on the
		//	   surface level the defect remains updated
		//	*) Only for full refinement and real coarser level, we can forget about
		//	   the defect on the base level, since only the correction is needed
		//	   on the higher level
			if(m_baseLev == m_topLev || m_bAdaptive)
			{
			//	get smoothing matrix
				SmartPtr<MatrixOperator<matrix_type, vector_type> > spSmoothMat
					= m_vLevData[lev]->get_smooth_mat();

			//	UPDATE DEFECT
				spSmoothMat->apply_sub(sd, sc);

			//	copy back to whole grid
				m_vLevData[lev]->copy_defect_from_smooth_patch(true);
				#ifdef UG_PARALLEL
					write_level_debug(d, "GMG_Def_AfterBaseSolver", lev);
				#endif
			}

		//	PROJECT CORRECTION BACK TO WHOLE GRID FOR PROLONGATION
			m_vLevData[lev]->copy_correction_from_smooth_patch(true);
			write_level_debug(*m_vLevData[lev]->c, "GMG_Cor_AfterBaseSolver", lev);
			GMG_PROFILE_END();
		}
		UG_DLOG(LIB_DISC_MULTIGRID, 3, " GMG: exiting serial basesolver branch.\n");
#ifdef UG_PARALLEL
	}

//	CASE b): We gather the processes, solve on one proc and distribute again
	else
	{
		UG_DLOG(LIB_DISC_MULTIGRID, 3, " GMG: entering parallel basesolver branch.\n");
	//	get whole grid correction
		vector_type& c = *m_vLevData[lev]->c;

		write_level_debug(d, "GMG_Def_BeforeGatherInBaseSolver", lev);

	//	gather the defect
		gather_vertical(d);

	//	Reset correction
		c.set(0.0);

		write_level_debug(d, "GMG_Def_BeforeBaseSolver", lev);

	//	check, if this proc continues, else idle
		if(d.layouts()->vertical_slave().empty())
		{
			GMG_PROFILE_BEGIN(GMG_BaseSolver);
			UG_DLOG(LIB_DISC_MULTIGRID, 3, " GMG: Start serial base solver.\n");

		//	compute coarse correction
			try{
				if(!m_spBaseSolver->apply(c, d))
					UG_THROW("AssembledMultiGridCycle::lmgc: Base solver on"
							" base level " << lev << " failed. "
							"(BaseLev="<<m_baseLev<<", TopLev="<<m_topLev<<")");
			}
			UG_CATCH_THROW("GMG: BaseSolver::apply failed. (case: b).")


//todo: is update defect really useful here?
		//	update defect
			if(m_baseLev == m_topLev)
				m_vLevData[m_baseLev]->spLevMat->apply_sub(d, c);
			GMG_PROFILE_END();
			UG_DLOG(LIB_DISC_MULTIGRID, 3, " GMG serial base solver done.\n");
		}


	//	broadcast the correction
		broadcast_vertical(c);
		c.set_storage_type(PST_CONSISTENT);
		write_level_debug(c, "GMG_Cor_AfterBaseSolver", lev);

//todo: is update defect really useful here?
	//	if baseLevel == surfaceLevel, we need also d
		//if((m_baseLev == m_topLev) || m_bAdaptive)
		if(m_baseLev == m_topLev)
		{
			d.set_storage_type(PST_CONSISTENT);
			broadcast_vertical(d);
			d.change_storage_type(PST_ADDITIVE);
			write_level_debug(d, "GMG_Def_AfterBaseSolver", lev);
		}

		UG_DLOG(LIB_DISC_MULTIGRID, 3, " GMG: exiting parallel basesolver branch.\n");
	}
#endif

	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop - base_solve on level " << lev << "\n");

	}
	UG_CATCH_THROW("GMG: Base Solver failed.");
}

// performs a  multi grid cycle on the level
template <typename TDomain, typename TAlgebra>
void AssembledMultiGridCycle<TDomain, TAlgebra>::
lmgc(size_t lev)
{
	PROFILE_FUNC_GROUP("gmg");
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-start - lmgc on level " << lev << "\n");

//	switch, if base level is reached. If so, call base Solver, else we will
//	perform smoothing, restrict the defect and call the lower level; then,
//	going up again in the level hierarchy the correction is interpolated and
//	used as coarse grid correction. Finally a post-smooth is performed.
	if((int)lev > m_baseLev)
	{
		for(int i = 0; i < m_cycleType; ++i)
		{
			m_vLevData[lev]->c->set(0.0); // <<<< only for debug

		//	UG_LOG("Before presmooth:\n");	log_level_data(lev);
			try{
				presmooth(lev);
			}
			UG_CATCH_THROW("AssembledMultiGridCycle::lmgc: presmooth failed on level "<<lev);

		//	UG_LOG("Before restriction:\n");	log_level_data(lev);
			try{
				restriction(lev);
			}
			UG_CATCH_THROW("AssembledMultiGridCycle::lmgc: restriction failed on level "<<lev);

			write_level_debug(*m_vLevData[lev]->d, "GMG__TestDefectBeforeLMGCRecursion", lev);

			try{
				lmgc(lev-1);
			}
			UG_CATCH_THROW("AssembledMultiGridCycle::lmgc: Linear multi grid "
							"cycle on level "<<lev-1<<" failed. (BaseLev="<<
							m_baseLev<<", TopLev="<<m_topLev<<").");

			write_level_debug(*m_vLevData[lev]->d, "GMG__TestDefectAfterLMGCRecursion", lev);

		//	UG_LOG("Before prolongation:\n");	log_level_data(lev);
			try{
				prolongation(lev);
			}
			UG_CATCH_THROW("AssembledMultiGridCycle::lmgc: prolongation failed on level "<<lev);

		//	UG_LOG("Before postsmooth:\n");	log_level_data(lev);
		//	note that the correction and defect at this time is are stored in
		//	the smooth-vectors only, if v-masters are present...
			write_smooth_level_debug(m_vLevData[lev]->get_smooth_defect(), "GMG_Def_BeforePostSmooth", lev);
			write_smooth_level_debug(m_vLevData[lev]->get_smooth_correction(), "GMG_Cor_BeforePostSmooth", lev);
			try{
				postsmooth(lev);
			}
			UG_CATCH_THROW("AssembledMultiGridCycle::lmgc: postsmooth failed on level "<<lev);
			write_smooth_level_debug(m_vLevData[lev]->get_smooth_defect(), "GMG_Def_AfterPostSmooth", lev);
			write_smooth_level_debug(m_vLevData[lev]->get_smooth_correction(), "GMG_Cor_AfterPostSmooth", lev);

		//	UG_LOG("After postsmooth:\n");	log_level_data(lev);
		}

		UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop - lmgc on level " << lev << "\n");
	}

//	if the base level has been reached, the coarse problem is solved exactly
	else if((int)lev == m_baseLev)
	{
		try{
			base_solve(lev);
		}
		UG_CATCH_THROW("AssembledMultiGridCycle::lmgc: basesolver failed on level "<<lev);

		UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop - lmgc on level " << lev << " (base solver executed)\n");
	}
//	this case should never happen.
	else {
		UG_THROW("AssembledMultiGridCycle::lmgc: Level index below baseLevel.");
	}
}

////////////////////////////////////////////////////////////////////////////////
// Debug Methods
////////////////////////////////////////////////////////////////////////////////

template <typename TDomain, typename TAlgebra>
void
AssembledMultiGridCycle<TDomain, TAlgebra>::
write_smooth_level_debug(const vector_type& vec, const char* filename, size_t lev)
{
	PROFILE_FUNC_GROUP("debug");
//	if no debug writer set, we're done
	if(m_spDebugWriter.invalid()) return;

//	cast dbg writer
	SmartPtr<GridFunctionDebugWriter<TDomain, TAlgebra> > dbgWriter =
			m_spDebugWriter.template cast_dynamic<GridFunctionDebugWriter<TDomain, TAlgebra> >();

//	set grid function
	if(dbgWriter.invalid()) UG_THROW("Cannot write debug vector on level");

//	add iter count to name
	std::string name(filename);
	char ext[20]; sprintf(ext, "_lev%03d_iter%03d.vec", (int)lev, m_dbgIterCnt);
	name.append(ext);

//	write
	GridLevel gridLev = dbgWriter->grid_level();
	dbgWriter->set_grid_level(GridLevel(lev,GridLevel::LEVEL));
	if(m_vLevData[lev]->has_ghosts())
		dbgWriter->set_map_global_to_patch(&m_vLevData[lev]->vMapGlobalToPatch);
	dbgWriter->write_vector(vec, name.c_str());
	dbgWriter->set_grid_level(gridLev);
	dbgWriter->set_map_global_to_patch(NULL);
}

template <typename TDomain, typename TAlgebra>
void
AssembledMultiGridCycle<TDomain, TAlgebra>::
write_smooth_level_debug(const matrix_type& mat, const char* filename, size_t lev)
{
	PROFILE_FUNC_GROUP("debug");
//	if no debug writer set, we're done
	if(m_spDebugWriter.invalid()) return;

//	cast dbg writer
	SmartPtr<GridFunctionDebugWriter<TDomain, TAlgebra> > dbgWriter =
			m_spDebugWriter.template cast_dynamic<GridFunctionDebugWriter<TDomain, TAlgebra> >();

//	set grid function
	if(dbgWriter.invalid()) UG_THROW("Cannot write debug matrix on level");

//	add iter count to name
	std::string name(filename);
	char ext[20]; sprintf(ext, "_lev%03d_iter%03d.mat", (int)lev, m_dbgIterCnt);
	name.append(ext);

//	write
	GridLevel gridLev = dbgWriter->grid_level();
	dbgWriter->set_grid_level(GridLevel(lev,GridLevel::LEVEL));
	if(m_vLevData[lev]->has_ghosts())
		dbgWriter->set_map_global_to_patch(&m_vLevData[lev]->vMapGlobalToPatch);
	dbgWriter->write_matrix(mat, name.c_str());
	dbgWriter->set_grid_level(gridLev);
	dbgWriter->set_map_global_to_patch(NULL);
}

template <typename TDomain, typename TAlgebra>
void
AssembledMultiGridCycle<TDomain, TAlgebra>::
write_level_debug(const vector_type& vec, const char* filename, size_t lev)
{
	PROFILE_FUNC_GROUP("debug");
//	if no debug writer set, we're done
	if(m_spDebugWriter.invalid()) return;

	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-start write_level_debug(vec): " << filename << "\n");
//	cast dbg writer
	SmartPtr<GridFunctionDebugWriter<TDomain, TAlgebra> > dbgWriter =
			m_spDebugWriter.template cast_dynamic<GridFunctionDebugWriter<TDomain, TAlgebra> >();

//	set grid function
	if(dbgWriter.invalid()) UG_THROW("Cannot write debug vector on level");

//	add iter count to name
	std::string name(filename);
	char ext[20]; sprintf(ext, "_lev%03d_iter%03d.vec", (int)lev, m_dbgIterCnt);
	name.append(ext);

//	write
	GridLevel gridLev = dbgWriter->grid_level();
	dbgWriter->set_grid_level(GridLevel(lev,GridLevel::LEVEL));
	dbgWriter->write_vector(vec, name.c_str());
	dbgWriter->set_grid_level(gridLev);
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop write_level_debug(vec): " << filename << "\n");
}

template <typename TDomain, typename TAlgebra>
void
AssembledMultiGridCycle<TDomain, TAlgebra>::
write_level_debug(const matrix_type& mat, const char* filename, size_t lev)
{
	PROFILE_FUNC_GROUP("debug");
//	if no debug writer set, we're done
	if(m_spDebugWriter.invalid()) return;

	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-start write_level_debug(mat): " << filename << "\n");
//	cast dbg writer
	SmartPtr<GridFunctionDebugWriter<TDomain, TAlgebra> > dbgWriter =
			m_spDebugWriter.template cast_dynamic<GridFunctionDebugWriter<TDomain, TAlgebra> >();

//	set grid function
	if(dbgWriter.invalid()) UG_THROW("Cannot write debug matrix on level");

//	add iter count to name
	std::string name(filename);
	char ext[20]; sprintf(ext, "_lev%03d_iter%03d.mat", (int)lev, m_dbgIterCnt);
	name.append(ext);

//	write
	GridLevel gridLev = dbgWriter->grid_level();
	dbgWriter->set_grid_level(GridLevel(lev,GridLevel::LEVEL));
	dbgWriter->write_matrix(mat, name.c_str());
	dbgWriter->set_grid_level(gridLev);
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop write_level_debug(mat): " << filename << "\n");
}

template <typename TDomain, typename TAlgebra>
void
AssembledMultiGridCycle<TDomain, TAlgebra>::
write_surface_debug(const vector_type& vec, const char* filename)
{
	PROFILE_FUNC_GROUP("debug");
//	if no debug writer set, we're done
	if(m_spDebugWriter.invalid()) return;

	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-start write_surface_debug(vec): " << filename << "\n");

//	cast dbg writer
	SmartPtr<GridFunctionDebugWriter<TDomain, TAlgebra> > dbgWriter =
			m_spDebugWriter.template cast_dynamic<GridFunctionDebugWriter<TDomain, TAlgebra> >();

//	set grid function
	if(dbgWriter.invalid()) UG_THROW("Cannot write debug vector on surface");

//	add iter count to name
	std::string name(filename);
	char ext[20]; sprintf(ext, "_surf_iter%03d.vec", m_dbgIterCnt);
	name.append(ext);

//	write
	GridLevel gridLev = dbgWriter->grid_level();
	dbgWriter->set_grid_level(GridLevel(GridLevel::TOP, GridLevel::SURFACE));
	dbgWriter->write_vector(vec, name.c_str());
	dbgWriter->set_grid_level(gridLev);
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop write_surface_debug(vec): " << filename << "\n");
}


template <typename TDomain, typename TAlgebra>
void
AssembledMultiGridCycle<TDomain, TAlgebra>::
write_surface_debug(const matrix_type& mat, const char* filename)
{
	PROFILE_FUNC_GROUP("debug");
//	if no debug writer set, we're done
	if(m_spDebugWriter.invalid()) return;

	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-start write_surface_debug(mat): " << filename << "\n");
//	cast dbg writer
	SmartPtr<GridFunctionDebugWriter<TDomain, TAlgebra> > dbgWriter =
			m_spDebugWriter.template cast_dynamic<GridFunctionDebugWriter<TDomain, TAlgebra> >();

//	set grid function
	if(dbgWriter.invalid()) UG_THROW("Cannot write debug matrix on surface");

//	add iter count to name
	std::string name(filename);
	char ext[20]; sprintf(ext, "_surf_iter%03d.mat", m_dbgIterCnt);
	name.append(ext);

//	write
	GridLevel gridLev = dbgWriter->grid_level();
	dbgWriter->set_grid_level(GridLevel(GridLevel::TOP, GridLevel::SURFACE));
	dbgWriter->write_matrix(mat, name.c_str());
	dbgWriter->set_grid_level(gridLev);
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop write_surface_debug(mat): " << filename << "\n");
}

template <typename TDomain, typename TAlgebra>
void
AssembledMultiGridCycle<TDomain, TAlgebra>::
log_level_data(size_t lvl)
{
	std::string prefix;
	if(lvl < m_vLevData.size())
		prefix.assign(2 + 2 * (m_vLevData.size() - lvl), ' ');

	LevData& ld = *m_vLevData[lvl];
	UG_LOG(prefix << "local d norm: " << sqrt(VecProd(*ld.d, *ld.d)) << std::endl);
	UG_LOG(prefix << "local c norm: " << sqrt(VecProd(*ld.c, *ld.c)) << std::endl);
	UG_LOG(prefix << "local smooth_d norm: " << sqrt(VecProd(ld.get_smooth_defect(), ld.get_smooth_defect())) << std::endl);
	UG_LOG(prefix << "local smooth_c norm: " << sqrt(VecProd(ld.get_smooth_correction(), ld.get_smooth_correction())) << std::endl);

	#ifdef UG_PARALLEL
		uint oldStorageMask = ld.d->get_storage_mask();
		number norm = ld.d->norm();
		UG_LOG(prefix << "parallel d norm: " << norm << "\n");
		if(oldStorageMask & PST_ADDITIVE)
			ld.d->change_storage_type(PST_ADDITIVE);
		else if(oldStorageMask & PST_CONSISTENT)
			ld.d->change_storage_type(PST_CONSISTENT);

		oldStorageMask = ld.c->get_storage_mask();
		norm = ld.c->norm();
		UG_LOG(prefix << "parallel c norm: " << norm << "\n");
		if(oldStorageMask & PST_ADDITIVE)
			ld.c->change_storage_type(PST_ADDITIVE);
		else if(oldStorageMask & PST_CONSISTENT)
			ld.c->change_storage_type(PST_CONSISTENT);

		oldStorageMask = ld.get_smooth_defect().get_storage_mask();
		norm = ld.get_smooth_defect().norm();
		UG_LOG(prefix << "parallel smooth defect norm: " << norm << "\n");
		if(oldStorageMask & PST_ADDITIVE)
			ld.get_smooth_defect().change_storage_type(PST_ADDITIVE);
		else if(oldStorageMask & PST_CONSISTENT)
			ld.get_smooth_defect().change_storage_type(PST_CONSISTENT);

		oldStorageMask = ld.get_smooth_correction().get_storage_mask();
		norm = ld.get_smooth_correction().norm();
		UG_LOG(prefix << "parallel smooth correction norm: " << norm << "\n");
		if(oldStorageMask & PST_ADDITIVE)
			ld.get_smooth_correction().change_storage_type(PST_ADDITIVE);
		else if(oldStorageMask & PST_CONSISTENT)
			ld.get_smooth_correction().change_storage_type(PST_CONSISTENT);
	#endif
}

////////////////////////////////////////////////////////////////////////////////
// Parallel Communication
////////////////////////////////////////////////////////////////////////////////

#ifdef UG_PARALLEL
template <typename TDomain, typename TAlgebra>
void
AssembledMultiGridCycle<TDomain, TAlgebra>::
gather_vertical(vector_type& d)
{
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-start - gather_vertical\n");
	PROFILE_FUNC_GROUP("gmg");

//	send vertical-slaves -> vertical-masters
	GMG_PROFILE_BEGIN(GMG_GatherVerticalVector);

	if(!d.layouts()->vertical_slave().empty()){
//		UG_DLOG_ALL_PROCS(LIB_DISC_MULTIGRID, 2,
//		  " Going down: SENDS vert. dofs.\n");

	//	there may be v-slaves with multiple v-masters. We only want to send
	//	a fraction to each master, to keep d additive.
	//	count number of occurrances in v-interfaces
		bool multiOccurance = false;
		std::vector<number> occurence;
		const IndexLayout& layout = d.layouts()->vertical_slave();

		if(layout.num_interfaces() > 1){
			occurence.resize(d.size(), 0);
			for(IndexLayout::const_iterator iiter = layout.begin();
				iiter != layout.end(); ++iiter)
			{
				const IndexLayout::Interface& itfc = layout.interface(iiter);
				for(IndexLayout::Interface::const_iterator iter = itfc.begin();
					iter != itfc.end(); ++iter)
				{
					const IndexLayout::Interface::Element& index = itfc.get_element(iter);

					occurence[index] += 1;
					if(occurence[index] > 1)
						multiOccurance = true;
				}
			}

		}
		if(multiOccurance){
		//todo: avoid copy tmp_d if possible.
			vector_type tmp_d(d.size());
			tmp_d.set_storage_type(d.get_storage_mask());
		//	we'll copy adjusted values from d to the occurances vector
			for(size_t i = 0; i < occurence.size(); ++i){
				if(occurence[i] > 0) // others can be ignored since not communicated anyways
					tmp_d[i] = d[i] * (1./occurence[i]);
			}
			ComPol_VecAdd<vector_type> cpVecAdd(&tmp_d);
			m_Com.send_data(d.layouts()->vertical_slave(), cpVecAdd);
		}
		else{
		//	schedule Sending of DoFs of vertical slaves
			ComPol_VecAdd<vector_type> cpVecAdd(&d);
			m_Com.send_data(d.layouts()->vertical_slave(), cpVecAdd);
		}
	}

	ComPol_VecAdd<vector_type> cpVecAddRcv(&d); // has to exist until communicate was executed
	if(!d.layouts()->vertical_master().empty()){
//		UG_DLOG_ALL_PROCS(LIB_DISC_MULTIGRID, 2,
//		 " Going down:  WAITS FOR RECIEVE of vert. dofs.\n");

	//	schedule Receive of DoFs on vertical masters
		m_Com.receive_data(d.layouts()->vertical_master(), cpVecAddRcv);
	}

//	perform communication
	m_Com.communicate();
	GMG_PROFILE_END();

	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop - gather_vertical\n");
}


template <typename TDomain, typename TAlgebra>
void
AssembledMultiGridCycle<TDomain, TAlgebra>::
broadcast_vertical(vector_type& t)
{
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-start - broadcast_vertical\n");
	PROFILE_FUNC_GROUP("gmg");
//	send vertical-masters -> vertical-slaves
	GMG_PROFILE_BEGIN(GMG_BroadcastVerticalVector);
	ComPol_VecCopy<vector_type> cpVecCopy(&t);
	if(!t.layouts()->vertical_slave().empty())
	{
//		UG_DLOG_ALL_PROCS(LIB_DISC_MULTIGRID, 2,
//		 " Going up: WAITS FOR RECIEVE of vert. dofs.\n");

	//	schedule slaves to receive correction
		m_Com.receive_data(t.layouts()->vertical_slave(), cpVecCopy);
	}

	if(!t.layouts()->vertical_master().empty())
	{
//		UG_DLOG_ALL_PROCS(LIB_DISC_MULTIGRID, 2,
//		 " Going up: SENDS vert. dofs.\n");

	//	schedule masters to send correction
		m_Com.send_data(t.layouts()->vertical_master(), cpVecCopy);
	}

//	communicate
	m_Com.communicate();
	GMG_PROFILE_END();
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop - broadcast_vertical\n");
}

template <typename TDomain, typename TAlgebra>
void
AssembledMultiGridCycle<TDomain, TAlgebra>::
broadcast_vertical_add(vector_type& d)
{
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-start - broadcast_vertical\n");
	PROFILE_FUNC_GROUP("gmg");
//	send vertical-masters -> vertical-slaves
	GMG_PROFILE_BEGIN(GMG_BroadcastVerticalVector);

//	deadly v-interfaces of crossing death make the resulting defect consistent
//	so that we have to transform it to additive unique
	if(!d.layouts()->vertical_master().empty()){
//		UG_DLOG_ALL_PROCS(LIB_DISC_MULTIGRID, 2,
//		  " Going down: SENDS vert. dofs.\n");

	//	there may be v-slaves with multiple v-masters. We only want to send
	//	a fraction to each master, to keep d additive.
	//	count number of occurrances in v-interfaces
		bool multiOccurance = false;
		std::vector<number> occurence;
		const IndexLayout& layout = d.layouts()->vertical_master();

		if(layout.num_interfaces() > 1){
			occurence.resize(d.size(), 0);
			for(IndexLayout::const_iterator iiter = layout.begin();
				iiter != layout.end(); ++iiter)
			{
				const IndexLayout::Interface& itfc = layout.interface(iiter);
				for(IndexLayout::Interface::const_iterator iter = itfc.begin();
					iter != itfc.end(); ++iter)
				{
					const IndexLayout::Interface::Element& index = itfc.get_element(iter);

					occurence[index] += 1;
					if(occurence[index] > 1)
						multiOccurance = true;
				}
			}

		}
		if(multiOccurance){
		//todo: avoid copy tmp_d if possible.
			vector_type tmp_d(d.size());
			tmp_d.set_storage_type(d.get_storage_mask());
		//	we'll copy adjusted values from d to the occurances vector
			for(size_t i = 0; i < occurence.size(); ++i){
				if(occurence[i] > 0) // others can be ignored since not communicated anyways
					tmp_d[i] = d[i] * (1./occurence[i]);
			}
			ComPol_VecAdd<vector_type> cpVecAdd(&tmp_d);
			m_Com.send_data(d.layouts()->vertical_master(), cpVecAdd);
		}
		else{
		//	schedule Sending of DoFs of vertical slaves
			ComPol_VecAdd<vector_type> cpVecAdd(&d);
			m_Com.send_data(d.layouts()->vertical_master(), cpVecAdd);
		}
	}

	ComPol_VecAdd<vector_type> cpVecAdd(&d);
	if(!d.layouts()->vertical_slave().empty())
	{
//		UG_DLOG_ALL_PROCS(LIB_DISC_MULTIGRID, 2,
//		 " Going up: WAITS FOR RECIEVE of vert. dofs.\n");

	//	schedule slaves to receive correction
		m_Com.receive_data(d.layouts()->vertical_slave(), cpVecAdd);
	}

//	communicate
	m_Com.communicate();

	GMG_PROFILE_END();
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop - broadcast_vertical\n");
}

template <typename TDomain, typename TAlgebra>
void
AssembledMultiGridCycle<TDomain, TAlgebra>::
copy_to_vertical_masters(vector_type& c)
{
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-start - copy_to_vertical_masters\n");
	PROFILE_FUNC_GROUP("gmg");
//	send vertical-slaves -> vertical-masters
	GMG_PROFILE_BEGIN(GMG_CopyToVerticalMasters);
	ComPol_VecCopy<vector_type> cpVecCopy(&c);

	if(!c.layouts()->vertical_master().empty())
		m_Com.receive_data(c.layouts()->vertical_master(), cpVecCopy);

	if(!c.layouts()->vertical_slave().empty())
		m_Com.send_data(c.layouts()->vertical_slave(), cpVecCopy);

//	communicate
	m_Com.communicate();
	GMG_PROFILE_END();
	UG_DLOG(LIB_DISC_MULTIGRID, 3, "gmg-stop - copy_to_vertical_masters\n");
}

template <typename TDomain, typename TAlgebra>
std::string
AssembledMultiGridCycle<TDomain, TAlgebra>::
config_string() const
{
	std::stringstream ss;
	ss << "GeometricMultigrid (";
	if(m_cycleType == 1) ss << "V-Cycle";
	else if(m_cycleType == 2) ss << "W-Cycle";
	else ss << " " << m_cycleType << "-Cycle";
	ss << ")\n";

	if(m_spPreSmootherPrototype==m_spPostSmootherPrototype)
		ss 	<< " Smoother (" << m_numPreSmooth << "x pre, " << m_numPostSmooth << "x post): "
			<< ConfigShift(m_spPreSmootherPrototype->config_string());
	else
	{
		ss << " Presmoother (" << m_numPreSmooth << "x): " << ConfigShift(m_spPreSmootherPrototype->config_string());
		ss << " Postsmoother ( " << m_numPostSmooth << "x): " << ConfigShift(m_spPostSmootherPrototype->config_string());
	}
	ss << "\n";
	ss << " Basesolver ( Baselevel = " << m_baseLev << ", parallel = " << (m_bBaseParallel ? "true" : "false") << "): ";
	ss << ConfigShift(m_spBaseSolver->config_string());
	return ss.str();

}

#endif

} // namespace ug


#endif /* __H__UG__LIB_DISC__MULTI_GRID_SOLVER__MG_SOLVER_IMPL__ */
