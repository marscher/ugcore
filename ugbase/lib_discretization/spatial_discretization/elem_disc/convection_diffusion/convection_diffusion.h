/*
 * convection_diffusion.h
 *
 *  Created on: 26.02.2010
 *      Author: andreasvogel
 */

#ifndef __H__LIB_DISCRETIZATION__SPATIAL_DISCRETIZATION__ELEM_DISC__CONVECTION_DIFFUSION__FV1__CONVECTION_DIFFUSION__
#define __H__LIB_DISCRETIZATION__SPATIAL_DISCRETIZATION__ELEM_DISC__CONVECTION_DIFFUSION__FV1__CONVECTION_DIFFUSION__

// other ug4 modules
#include "common/common.h"
#include "lib_grid/lg_base.h"

// library intern headers
#include "lib_discretization/spatial_discretization/elem_disc/elem_disc_interface.h"
#include "lib_discretization/spatial_discretization/ip_data/data_import_export.h"

#include "lib_discretization/spatial_discretization/disc_util/conv_shape_interface.h"

namespace ug{

/// \ingroup lib_disc_elem_disc
/// @{

/// Discretization for the Convection-Diffusion Equation
/**
 * This class implements the IElemDisc interface to provide element local
 * assemblings for the convection diffusion equation.
 * The Equation has the form
 * \f[
 * 	\partial_t (m c) - \nabla \left( D \nabla c - \vec{v} c \right) + r \cdot c
 * 		= f
 * \f]
 * with
 * <ul>
 * <li>	\f$ c \f$ is the unknown solution
 * <li>	\f$ m \equiv m(\vec{x},t) \f$ is the Mass Scaling Term
 * <li>	\f$ D \equiv D(\vec{x},t) \f$ is the Diffusion Tensor
 * <li>	\f$ v \equiv \vec{v}(\vec{x},t) \f$ is the Velocity Field
 * <li>	\f$ r \equiv r(\vec{x},t) \f$ is the Reaction Term
 * <li>	\f$ f \equiv f(\vec{x},t) \f$ is a Source Term
 * </ul>
 *
 * \tparam	TDomain		Domain
 * \tparam	TAlgebra	Algebra
 */
template<	typename TDomain>
class ConvectionDiffusionElemDisc
: public IDomainElemDisc<TDomain>
{
	private:
	///	Base class type
		typedef IDomainElemDisc<TDomain> base_type;

	///	Own type
		typedef ConvectionDiffusionElemDisc<TDomain> this_type;

	public:
	///	Domain type
		typedef typename base_type::domain_type domain_type;

	///	World dimension
		static const int dim = base_type::dim;

	///	Position type
		typedef typename base_type::position_type position_type;

	///	Local matrix type
		typedef typename base_type::local_matrix_type local_matrix_type;

	///	Local vector type
		typedef typename base_type::local_vector_type local_vector_type;

	///	Local index type
		typedef typename base_type::local_index_type local_index_type;

	public:
	///	Constructor
		ConvectionDiffusionElemDisc();

	///	set the upwind method
	/**
	 * This method sets the upwind method used to upwind the convection.
	 *
	 * \param	shapes		upwind method
	 */
		void set_upwind(IConvectionShapes<dim>& shapes);

	///	sets the diffusion tensor
	/**
	 * This method sets the Diffusion tensor used in computations. If no
	 * Tensor is set, a zero value is assumed.
	 */
		void set_diffusion(IPData<MathMatrix<dim, dim>, dim>& user);

	///	sets the velocity field
	/**
	 * This method sets the Velocity field. If no field is provided a zero
	 * value is assumed.
	 */
		void set_velocity(IPData<MathVector<dim>, dim>& user);

	///	sets the reaction
	/**
	 * This method sets the Reaction. A zero value is assumed as default.
	 */
		void set_reaction(IPData<number, dim>& user);

	///	sets the source / sink term
	/**
	 * This method sets the source/sink value. A zero value is assumed as
	 * default.
	 */
		void set_source(IPData<number, dim>& user);

	///	sets mass scale
	/**
	 * This method sets the mass scale value. A value of 1.0 is assumed as
	 * default.
	 */
		void set_mass_scale(IPData<number, dim>& user);

	public:
	///	number of functions used
		virtual size_t num_fct();

	///	type of trial space for each function used
		virtual bool request_finite_element_id(const std::vector<LFEID>& vLfeID);

	///	switches between non-regular and regular grids
		virtual bool treat_non_regular_grid(bool bNonRegular);

	///	returns if hanging nodes are needed
		virtual bool use_hanging() const;

	///	sets the disc scheme
		void set_disc_scheme(const char* c_scheme);

	protected:
	///	sets the requested assembling routines
		void set_assemble_funcs();

	///	current type of disc scheme
		std::string m_discScheme;

	///	current order of disc scheme
		int m_order;

	///	current regular grid flag
		bool m_bNonRegularGrid;

	private:
	///	prepares the discretization for time dependent discretization
	/**
	 * This function prepares the discretization for time-dependent problems.
	 * It sets the time in the imports.
	 *
	 * \param[in]	time	new time point
	 * \returns 	true	indicates, that old values are needed
	 */
		virtual bool time_point_changed(number time);

		/////////////////////////////////////
		//	Finite Volume assemblings
		/////////////////////////////////////

	///	prepares the loop over all elements
	/**
	 * This method prepares the loop over all elements. It resizes the Position
	 * array for the corner coordinates and schedules the local ip positions
	 * at the data imports.
	 */
		template <typename TElem, typename TFVGeom>
		inline bool elem_loop_prepare_fv1();

	///	prepares the element for assembling
	/**
	 * This methods prepares an element for the assembling. The Positions of
	 * the Element Corners are read and the Finite Volume Geometry is updated.
	 * The global ip positions are scheduled at the data imports.
	 */
		template <typename TElem, typename TFVGeom>
		bool elem_prepare_fv1(TElem* elem, const local_vector_type& u);

	///	finishes the loop over all elements
		template <typename TElem, typename TFVGeom>
		inline bool elem_loop_finish_fv1();

	///	assembles the local stiffness matrix using a finite volume scheme
		template <typename TElem, typename TFVGeom>
		bool elem_JA_fv1(local_matrix_type& J, const local_vector_type& u);

	///	assembles the local mass matrix using a finite volume scheme
		template <typename TElem, typename TFVGeom>
		bool elem_JM_fv1(local_matrix_type& J, const local_vector_type& u);

	///	assembles the stiffness part of the local defect
		template <typename TElem, typename TFVGeom>
		bool elem_dA_fv1(local_vector_type& d, const local_vector_type& u);

	///	assembles the mass part of the local defect
		template <typename TElem, typename TFVGeom>
		bool elem_dM_fv1(local_vector_type& d, const local_vector_type& u);

	///	assembles the local right hand side
		template <typename TElem, typename TFVGeom>
		bool elem_rhs_fv1(local_vector_type& d);

		/////////////////////////////////////
		//	Finite Element assemblings
		/////////////////////////////////////

		template<typename TElem,
				 template <class Elem, int WorldDim> class TTrialSpace, int TOrderSpace,
				 template <class Elem, int WorldDim> class TQuadRule, int TOrderQuad>
		bool elem_loop_prepare_fe();

		template<typename TElem,
				 template <class Elem, int WorldDim> class TTrialSpace, int TOrderSpace,
				 template <class Elem, int WorldDim> class TQuadRule, int TOrderQuad>
		bool elem_prepare_fe(TElem* elem, const local_vector_type& u);

		template<typename TElem,
				 template <class Elem, int WorldDim> class TTrialSpace, int TOrderSpace,
				 template <class Elem, int WorldDim> class TQuadRule, int TOrderQuad>
		bool elem_loop_finish_fe();

		template<typename TElem,
				 template <class Elem, int WorldDim> class TTrialSpace, int TOrderSpace,
				 template <class Elem, int WorldDim> class TQuadRule, int TOrderQuad>
		bool elem_JA_fe(local_matrix_type& J, const local_vector_type& u);

		template<typename TElem,
				 template <class Elem, int WorldDim> class TTrialSpace, int TOrderSpace,
				 template <class Elem, int WorldDim> class TQuadRule, int TOrderQuad>
		bool elem_JM_fe(local_matrix_type& J, const local_vector_type& u);

		template<typename TElem,
				 template <class Elem, int WorldDim> class TTrialSpace, int TOrderSpace,
				 template <class Elem, int WorldDim> class TQuadRule, int TOrderQuad>
		bool elem_dA_fe(local_vector_type& d, const local_vector_type& u);

		template<typename TElem,
				 template <class Elem, int WorldDim> class TTrialSpace, int TOrderSpace,
				 template <class Elem, int WorldDim> class TQuadRule, int TOrderQuad>
		bool elem_dM_fe(local_vector_type& d, const local_vector_type& u);

		template<typename TElem,
				 template <class Elem, int WorldDim> class TTrialSpace, int TOrderSpace,
				 template <class Elem, int WorldDim> class TQuadRule, int TOrderQuad>
		bool elem_rhs_fe(local_vector_type& d);

	protected:
	///	computes the linearized defect w.r.t to the velocity
		template <typename TElem, typename TFVGeom>
		bool lin_defect_velocity_fv1(const local_vector_type& u);

	///	computes the linearized defect w.r.t to the velocity
		template <typename TElem, typename TFVGeom>
		bool lin_defect_diffusion_fv1(const local_vector_type& u);

	///	computes the linearized defect w.r.t to the reaction
		template <typename TElem, typename TFVGeom>
		bool lin_defect_reaction_fv1(const local_vector_type& u);

	///	computes the linearized defect w.r.t to the source term
		template <typename TElem, typename TFVGeom>
		bool lin_defect_source_fv1(const local_vector_type& u);

	///	computes the linearized defect w.r.t to the mass scale term
		template <typename TElem, typename TFVGeom>
		bool lin_defect_mass_scale_fv1(const local_vector_type& u);

	private:
	///	Corner Coordinates
		const position_type* m_vCornerCoords;

	///	abbreviation for the local solution
		static const size_t _C_ = 0;

	/// method to compute the upwind shapes
		IConvectionShapes<dim>* m_pConvShape;

	///	Data import for Diffusion
		DataImport<MathMatrix<dim,dim>, dim> m_imDiffusion;

	///	Data import for the Velocity field
		DataImport<MathVector<dim>, dim > m_imVelocity;

	///	Data import for the reaction term
		DataImport<number, dim> m_imReaction;

	///	Data import for the right-hand side
		DataImport<number, dim> m_imSource;

	///	Data import for the mass scale
		DataImport<number, dim> m_imMassScale;

	public:
		typedef IPData<number, dim> NumberExport;
		typedef IPData<MathVector<dim>, dim> GradExport;

	///	returns the export of the concentration
		IPData<number, dim>& get_concentration();

	///	returns the export of gradient of the concentration
		IPData<MathVector<dim>, dim>& get_concentration_grad();

	protected:
		typedef IConvectionShapes<dim> conv_shape_type;

	///	returns the updated convection shapes
		const IConvectionShapes<dim>& get_updated_conv_shapes(const FVGeometryBase& geo);

	///	computes the concentration
		template <typename TElem, typename TFVGeom>
		bool comp_export_concentration_fv1(const local_vector_type& u, bool compDeriv);

	///	computes the gradient of the concentration
		template <typename TElem, typename TFVGeom>
		bool comp_export_concentration_grad_fv1(const local_vector_type& u, bool compDeriv);

	///	Export for the concentration
		DataExport<number, dim> m_exConcentration;

	///	Export for the gradient of concentration
		DataExport<MathVector<dim>, dim> m_exConcentrationGrad;

	protected:
	// 	FV1 Assemblings
		void register_all_fv1_funcs(bool bHang);

		template <template <class Elem, int WorldDim> class TFVGeom>
		struct RegisterFV1 {
				RegisterFV1(this_type* pThis) : m_pThis(pThis){}
				this_type* m_pThis;
				template< typename TElem > void operator()(TElem&)
				{m_pThis->register_fv1_func<TElem, TFVGeom<TElem, dim> >();}
		};

		template <template <int TDim, int WorldDim> class TFVGeom>
		struct RegisterDimFV1 {
				RegisterDimFV1(this_type* pThis) : m_pThis(pThis){}
				this_type* m_pThis;
				template< typename TElem > void operator()(TElem&)
				{
					static const int refDim = reference_element_traits<TElem>::dim;
					m_pThis->register_fv1_func<TElem, TFVGeom<refDim, dim> >();
				}
		};

		template <typename TElem, typename TFVGeom>
		void register_fv1_func();

	// 	FE Assemblings
		void register_all_fe_funcs(int order);

		struct RegisterFE {
				RegisterFE(this_type* pThis, int p);
				this_type* m_pThis; int m_p;
				template< typename TElem > void operator()(TElem&);
		};

		template<typename TElem,
				 template <class Elem, int WorldDim> class TTrialSpace, int TOrderSpace,
				 template <class Elem, int WorldDim> class TQuadRule, int TOrderQuad>
		void register_fe_func();

};

/// @}

} // end namespace ug


#endif /*__H__LIB_DISCRETIZATION__SPATIAL_DISCRETIZATION__ELEM_DISC__CONVECTION_DIFFUSION__FV1__CONVECTION_DIFFUSION__*/
