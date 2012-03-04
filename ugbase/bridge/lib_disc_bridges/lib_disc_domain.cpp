/*
 * lib_disc_bridge_domain_dependent.cpp
 *
 *  Created on: 22.09.2010
 *      Author: andreasvogel
 */

// extern headers
#include <iostream>
#include <sstream>
#include <string>

// include bridge
#include "../bridge.h"
#include "registry/registry.h"

// lib_algebra includes
#include "lib_algebra/cpu_algebra_types.h"
#include "lib_algebra/operator/operator_util.h"
#include "lib_algebra/operator/operator_interface.h"
#include "lib_algebra/operator/operator_inverse_interface.h"

// lib_disc includes
#include "lib_disc/domain.h"
#include "lib_disc/function_spaces/grid_function.h"
#include "lib_disc/function_spaces/approximation_space.h"
#include "lib_disc/function_spaces/grid_function_util.h"
#include "lib_disc/function_spaces/interpolate.h"
#include "lib_disc/function_spaces/integrate.h"
#include "lib_disc/function_spaces/integrateDraft.h"
#include "lib_disc/function_spaces/error_indicator.h"
#include "lib_disc/dof_manager/cuthill_mckee.h"
#include "lib_disc/dof_manager/lexorder.h"

#include "lib_disc/spatial_disc/domain_disc.h"
#include "lib_disc/spatial_disc/elem_disc/elem_disc_interface.h"
#include "lib_disc/spatial_disc/constraints/constraint_interface.h"
#include "lib_disc/spatial_disc/constraints/dirichlet_boundary/lagrange_dirichlet_boundary.h"
#include "lib_disc/spatial_disc/constraints/continuity_constraints/p1_continuity_constraints.h"

//#include "lib_disc/operator/non_linear_operator/sqp_method/sqp.h"

using namespace std;

namespace ug
{

namespace bridge
{

template <typename TDomain, typename TAlgebra>
void RegisterLibDiscDomain__Algebra_Domain(Registry& reg, string parentGroup)
{
//	typedef
	static const int dim = TDomain::dim;
	typedef typename TAlgebra::vector_type vector_type;
	typedef typename TAlgebra::matrix_type matrix_type;
	typedef ApproximationSpace<TDomain> approximation_space_type;

//	group string
	string approxGrp = parentGroup; approxGrp.append("/ApproximationSpace");

//	suffix and tag
	string dimAlgSuffix = GetDomainSuffix<TDomain>();
	dimAlgSuffix.append(GetAlgebraSuffix<TAlgebra>());

	string dimAlgTag = GetDomainTag<TDomain>();
	dimAlgTag.append(GetAlgebraTag<TAlgebra>());

	string domDiscGrp = parentGroup; domDiscGrp.append("/SpatialDisc");

//	DomainDiscretization
	{
		typedef IDomainDiscretization<TAlgebra> TBase;
		typedef DomainDiscretization<TDomain, TAlgebra> T;
		string name = string("DomainDiscretization").append(dimAlgSuffix);
		reg.add_class_<T, TBase>(name, domDiscGrp)
			.template add_constructor<void (*)(SmartPtr<ApproximationSpace<TDomain> >)>("ApproximationSpace")
			.add_method("add", static_cast<void (T::*)(IDomainConstraint<TDomain, TAlgebra>&)>(&T::add), "", "Post Process")
			.add_method("add", static_cast<void (T::*)(IDomainElemDisc<TDomain>&)>(&T::add), "", "Element Discretization")
			.add_method("add", static_cast<void (T::*)(IDiscretizationItem<TDomain, TAlgebra>&)>(&T::add), "", "DiscItem");
		reg.add_class_to_group(name, "DomainDiscretization", dimAlgTag);
	}

//	IDomainConstraint
	{
		std::string grp = parentGroup; grp.append("/Discretization/SpatialDisc");
		typedef IConstraint<TAlgebra> TBase;
		typedef IDomainConstraint<TDomain, TAlgebra> T;
		string name = string("IDomainConstraint").append(dimAlgSuffix);
		reg.add_class_<T, TBase>(name, grp);
		reg.add_class_to_group(name, "IDomainConstraint", dimAlgTag);
	}

//	OneSideP1ConstraintsPostProcess
	{
		std::string grp = parentGroup; grp.append("/Discretization/SpatialDisc");
		typedef OneSideP1ConstraintsPostProcess<TDomain, TAlgebra> T;
		typedef IDomainConstraint<TDomain, TAlgebra> baseT;
		string name = string("OneSideP1Constraints").append(dimAlgSuffix);
		reg.add_class_<T, baseT>(name, grp)
			.add_constructor();
		reg.add_class_to_group(name, "OneSideP1Constraints", dimAlgTag);
	}

//	SymP1ConstraintsPostProcess
	{
		std::string grp = parentGroup; grp.append("/Discretization/SpatialDisc");
		typedef SymP1ConstraintsPostProcess<TDomain, TAlgebra> T;
		typedef IDomainConstraint<TDomain, TAlgebra> baseT;
		string name = string("SymP1Constraints").append(dimAlgSuffix);
		reg.add_class_<T, baseT>(name, grp)
			.add_constructor();
		reg.add_class_to_group(name, "SymP1Constraints", dimAlgTag);
	}

//	LagrangeDirichletBoundary
	{
		typedef boost::function<bool (number& value, const MathVector<dim>& x, number time)> BNDNumberFunctor;
		typedef boost::function<void (number& value, const MathVector<dim>& x, number time)> NumberFunctor;
		typedef LagrangeDirichletBoundary<TDomain, TAlgebra> T;
		typedef IDomainConstraint<TDomain, TAlgebra> TBase;
		string name = string("DirichletBoundary").append(dimAlgSuffix);
		reg.add_class_<T, TBase>(name, domDiscGrp)
			.add_constructor()
			.add_method("add", static_cast<void (T::*)(BNDNumberFunctor&, const char*, const char*)>(&T::add),
						"Success", "Value#Function#Subsets")
			.add_method("add", static_cast<void (T::*)(NumberFunctor&, const char*, const char*)>(&T::add),
						"Success", "Value#Function#Subsets")
			.add_method("add",static_cast<void (T::*)(number, const char*, const char*)>(&T::add),
						"Success", "Constant Value#Function#Subsets")
			.add_method("clear", &T::clear);
		reg.add_class_to_group(name, "DirichletBoundary", dimAlgTag);
	}

//	IDiscretizationItem
	{
		typedef IDiscretizationItem<TDomain, TAlgebra> T;
		string name = string("IDiscretizationItem").append(dimAlgSuffix);
		reg.add_class_<T>(name, domDiscGrp);
		reg.add_class_to_group(name, "IDiscretizationItem", dimAlgTag);
	}


//	MarkForRefinement_GradientIndicator
	{
		string grp("ug4/Refinement/");
		reg.add_function("MarkForRefinement_GradientIndicator",
						 &MarkForRefinement_GradientIndicator<TDomain, SurfaceDoFDistribution, TAlgebra>, grp);
	}
}


template <typename TGridFct>
void RegisterLibDiscDomain__GridFunction(Registry& reg, string parentGroup)
{
//	typedef
	static const int dim = TGridFct::dim;
	typedef typename TGridFct::algebra_type algebra_type;
	typedef typename algebra_type::vector_type vector_type;
	typedef typename algebra_type::matrix_type matrix_type;
	typedef typename TGridFct::approximation_space_type approximation_space_type;

#ifdef UG_PARALLEL
typedef ParallelGridFunction<TGridFct> TFct;
#else
typedef TGridFct TFct;
#endif

//	group string
	string approxGrp = parentGroup; approxGrp.append("/ApproximationSpace");

//	suffix and tag
	string dimAlgSuffix = GetDomainSuffix<dim>();
	dimAlgSuffix.append(GetAlgebraSuffix<algebra_type>());

	string dimAlgTag = GetDomainTag<dim>();
	dimAlgTag.append(GetAlgebraTag<algebra_type>());

//	GridFunction
	{
		string name = string("GridFunction").append(dimAlgSuffix);
		reg.add_class_<TFct, vector_type>(name, approxGrp)
			.template add_constructor<void (*)(SmartPtr<approximation_space_type>)>("ApproximationSpace")
			.add_method("assign", static_cast<void (TFct::*)(const vector_type&)>(&TFct::assign),
						"Success", "Vector")
			.add_method("clone", &TFct::clone);
		reg.add_class_to_group(name, "GridFunction", dimAlgTag);
	}

	string grp = parentGroup; grp.append("");

//	InterpolateFunction
	{
		typedef bool (*fct_type)(
				const boost::function<void (number& res,const MathVector<dim>& x, number time)>&,
				TFct&, const char*, number);
		reg.add_function("InterpolateFunction",
						 static_cast<fct_type>(&InterpolateFunction<TFct>),
						 grp);

		typedef bool (*fct_type_subset)(
				const boost::function<void (number& res,const MathVector<dim>& x, number time)>&,
				TFct&, const char*, number, const char*);
		reg.add_function("InterpolateFunction",
						 static_cast<fct_type_subset>(&InterpolateFunction<TFct>),
						 grp);
	}

//	L2Error
	{
		typedef number (*fct_type)(
				const boost::function<void (number& res,const MathVector<dim>& x, number time)>&,
				TFct&, const char*, number);
		reg.add_function("L2Error",
						 static_cast<fct_type>(&L2Error<TFct>),
						 grp);

		typedef number (*fct_type_subset)(
				const boost::function<void (number& res,const MathVector<dim>& x, number time)>&,
				TFct&, const char*, number, const char*);
		reg.add_function("L2Error",
						 static_cast<fct_type_subset>(&L2Error<TFct>),
						 grp);
	}

//	L2ErrorDraft
	{
		typedef number (*fct_type)(
				const boost::function<void (number& res,const MathVector<dim>& x, number time)>&,
				TFct&, const char*, number, int, const char*);
		reg.add_function("L2ErrorDraft",
						 static_cast<fct_type>(&L2ErrorDraft<TFct>),
						 grp);
	}

	//	L2Norm
		{
			typedef number (*fct_type)(TFct&, const char*, int, const char*);


			reg.add_function("L2Norm",
							 static_cast<fct_type>(&L2Norm<TFct>),
							 grp);
		}
}

template <typename TAlgebra>
static bool RegisterLibDiscDomain__Algebra(Registry& reg, string parentGroup)
{
//	get group string
	string grp = parentGroup; grp.append("/Discretization");

	try
	{

#ifdef UG_DIM_1
		RegisterLibDiscDomain__Algebra_Domain<Domain1d, TAlgebra>(reg, grp);
		RegisterLibDiscDomain__GridFunction<GridFunction<Domain1d, SurfaceDoFDistribution, TAlgebra> >(reg, grp);
#endif

#ifdef UG_DIM_2
		RegisterLibDiscDomain__Algebra_Domain<Domain2d, TAlgebra>(reg, grp);
		RegisterLibDiscDomain__GridFunction<GridFunction<Domain2d, SurfaceDoFDistribution, TAlgebra> >(reg, grp);
#endif

#ifdef UG_DIM_3
		RegisterLibDiscDomain__Algebra_Domain<Domain3d, TAlgebra>(reg, grp);
		RegisterLibDiscDomain__GridFunction<GridFunction<Domain3d, SurfaceDoFDistribution, TAlgebra> >(reg, grp);
#endif

	}
	catch(UG_REGISTRY_ERROR_RegistrationFailed ex)
	{
		UG_LOG("### ERROR in RegisterLibDiscDomain__Algebra: "
				"Registration failed (using name " << ex.name << ").\n");
		return false;
	}

	return true;
}

template <typename TDomain>
void RegisterLibDiscDomain__Domain(Registry& reg, string parentGroup)
{
//	typedef
//	static const int dim = TDomain::dim;
	typedef ApproximationSpace<TDomain> approximation_space_type;

//	group string
	string approxGrp = parentGroup; approxGrp.append("/ApproximationSpace");

//	suffix and tag
	string dimSuffix = GetDomainSuffix<TDomain>();
	string dimTag = GetDomainTag<TDomain>();

//	Order Cuthill-McKee
	{
		reg.add_function("OrderCuthillMcKee", static_cast<void (*)(approximation_space_type&, bool)>(&OrderCuthillMcKee), approxGrp);
	}

//	Order lexicographically
	{
		reg.add_function("OrderLex", static_cast<void (*)(approximation_space_type&, const char*)>(&OrderLex<TDomain>), approxGrp);
	}
}

bool RegisterLibDisc_Domain(Registry& reg, string parentGroup)
{
	reg.add_class_<GridLevel>("GridLevel")
		.add_constructor();

	bool bReturn = true;
#ifdef UG_CPU_1
	bReturn &= RegisterLibDiscDomain__Algebra<CPUAlgebra>(reg, parentGroup);
#endif
#ifdef UG_CPU_2
	bReturn &= RegisterLibDiscDomain__Algebra<CPUBlockAlgebra<2> >(reg, parentGroup);
#endif
#ifdef UG_CPU_3
	bReturn &= RegisterLibDiscDomain__Algebra<CPUBlockAlgebra<3> >(reg, parentGroup);
#endif
#ifdef UG_CPU_4
	bReturn &= RegisterLibDiscDomain__Algebra<CPUBlockAlgebra<4> >(reg, parentGroup);
#endif
#ifdef UG_CPU_VAR
	bReturn &= RegisterLibDiscDomain__Algebra<CPUVariableBlockAlgebra >(reg, parentGroup);
#endif

	try
	{
#ifdef UG_DIM_1
	RegisterLibDiscDomain__Domain<Domain1d>(reg, parentGroup);
#endif
#ifdef UG_DIM_2
	RegisterLibDiscDomain__Domain<Domain2d>(reg, parentGroup);
#endif
#ifdef UG_DIM_3
	RegisterLibDiscDomain__Domain<Domain3d>(reg, parentGroup);
#endif
	}
	catch(UG_REGISTRY_ERROR_RegistrationFailed ex)
	{
		UG_LOG("### ERROR in RegisterLibDiscDomain__Domain: "
				"Registration failed (using name " << ex.name << ").\n");
		return false;
	}

	return bReturn;
}

}//	end of namespace ug
}//	end of namespace interface
