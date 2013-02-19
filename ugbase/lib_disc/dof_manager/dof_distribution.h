/*
 * level_dof_distribution.h
 *
 *  Created on: 24.01.2012
 *      Author: andreasvogel
 */

#ifndef __H__UG__LIB_DISC__DOF_MANAGER__DOF_DISTRIBUTION__
#define __H__UG__LIB_DISC__DOF_MANAGER__DOF_DISTRIBUTION__

#include "lib_grid/tools/surface_view.h"
#include "lib_disc/domain_traits.h"
#include "mg_dof_distribution.h"

namespace ug{


class DoFDistribution
{
	public:
	///	constructor
		DoFDistribution(SmartPtr<MGDoFDistribution> spMGDD,
		                SmartPtr<SurfaceView> spSurfView,
		                const GridLevel& level)
			: m_spMGDD(spMGDD), m_spSurfView(spSurfView), m_level(level)
		{}

	///	returns the surface view
		ConstSmartPtr<SurfaceView> surface_view() const {return m_spSurfView;}

	///	returns the multi grid
		const MultiGrid& multi_grid() const {return *m_spSurfView->subset_handler()->multi_grid();}

	///	returns grid level
		const GridLevel& grid_level() const {return m_level;}

		///	returns if dofs are grouped
		bool grouped() const {return m_spMGDD->grouped();}

		///	returns blocksize
		std::string blocksize() const {return m_spMGDD->blocksize();}

	public:
		template <typename TElem>
		struct traits
		{
			typedef TElem geometric_object;
			typedef typename SurfaceView::traits<TElem>::iterator iterator;
			typedef typename SurfaceView::traits<TElem>::const_iterator const_iterator;
		};

		template <int dim>
		struct dim_traits
		{
			typedef typename domain_traits<dim>::geometric_base_object geometric_base_object;
			typedef typename SurfaceView::traits<geometric_base_object>::iterator iterator;
			typedef typename SurfaceView::traits<geometric_base_object>::const_iterator const_iterator;
		};

	/// iterator for elements where dofs are defined
	/// \{
		template <typename TElem>
		typename traits<TElem>::iterator begin() {return m_spSurfView->begin<TElem>(m_level);}

		template <typename TElem>
		typename traits<TElem>::iterator end() {return m_spSurfView->end<TElem>(m_level);}

		template <typename TElem>
		typename traits<TElem>::const_iterator begin() const {return m_spSurfView->begin<TElem>(m_level);}

		template <typename TElem>
		typename traits<TElem>::const_iterator end() const {return m_spSurfView->end<TElem>(m_level);}
	///	\}

	/// iterator for elements where dofs are defined
	/// \{
		template <typename TElem>
		typename traits<TElem>::iterator begin(int si) {return m_spSurfView->begin<TElem>(si, m_level);}

		template <typename TElem>
		typename traits<TElem>::iterator end(int si) {return m_spSurfView->end<TElem>(si, m_level);}

		template <typename TElem>
		typename traits<TElem>::const_iterator begin(int si) const {return m_spSurfView->begin<TElem>(si, m_level);}

		template <typename TElem>
		typename traits<TElem>::const_iterator end(int si) const {return m_spSurfView->end<TElem>(si, m_level);}
	///	\}

	public:
	///	type of multiindices
		typedef MGDoFDistribution::multi_index_type multi_index_type;

	///	returns all indices of the element
	///	\{
		void indices(VertexBase* elem, LocalIndices& ind, bool bHang = false) const{m_spMGDD->indices(elem, ind, bHang);}
		void indices(EdgeBase* elem, LocalIndices& ind, bool bHang = false) const{m_spMGDD->indices(elem, ind, bHang);}
		void indices(Face* elem, LocalIndices& ind, bool bHang = false) const{m_spMGDD->indices(elem, ind, bHang);}
		void indices(Volume* elem, LocalIndices& ind, bool bHang = false) const{m_spMGDD->indices(elem, ind, bHang);}
	/// \}

	/// get multi indices (Element + Closure of Element)
	/// \{
		size_t multi_indices(VertexBase* elem, size_t fct, std::vector<multi_index_type>& ind) const{return m_spMGDD->multi_indices(elem, fct, ind);}
		size_t multi_indices(EdgeBase* elem, size_t fct, std::vector<multi_index_type>& ind) const{return m_spMGDD->multi_indices(elem, fct, ind);}
		size_t multi_indices(Face* elem, size_t fct, std::vector<multi_index_type>& ind) const{return m_spMGDD->multi_indices(elem, fct, ind);}
		size_t multi_indices(Volume* elem, size_t fct, std::vector<multi_index_type>& ind) const{return m_spMGDD->multi_indices(elem, fct, ind);}
	/// \}

	/// get multi indices (only inner part of Element)
	/// \{
		size_t inner_multi_indices(VertexBase* elem, size_t fct, std::vector<multi_index_type>& ind) const{return m_spMGDD->inner_multi_indices(elem, fct, ind);}
		size_t inner_multi_indices(EdgeBase* elem, size_t fct, std::vector<multi_index_type>& ind) const{return m_spMGDD->inner_multi_indices(elem, fct, ind);}
		size_t inner_multi_indices(Face* elem, size_t fct, std::vector<multi_index_type>& ind) const{return m_spMGDD->inner_multi_indices(elem, fct, ind);}
		size_t inner_multi_indices(Volume* elem, size_t fct, std::vector<multi_index_type>& ind) const{return m_spMGDD->inner_multi_indices(elem, fct, ind);}
	/// \}

	/// get algebra indices (Element + Closure of Element)
	/// \{
		size_t algebra_indices(VertexBase* elem, std::vector<size_t>& ind) const{return m_spMGDD->algebra_indices(elem, ind);}
		size_t algebra_indices(EdgeBase* elem, std::vector<size_t>& ind) const{return m_spMGDD->algebra_indices(elem, ind);}
		size_t algebra_indices(Face* elem, std::vector<size_t>& ind) const{return m_spMGDD->algebra_indices(elem, ind);}
		size_t algebra_indices(Volume* elem, std::vector<size_t>& ind) const{return m_spMGDD->algebra_indices(elem, ind);}
	/// \}

	/// get algebra indices (only inner part of Element)
	/// \{
		size_t inner_algebra_indices(VertexBase* elem, std::vector<size_t>& ind) const{return m_spMGDD->inner_algebra_indices(elem,ind);}
		size_t inner_algebra_indices(EdgeBase* elem, std::vector<size_t>& ind) const{return m_spMGDD->inner_algebra_indices(elem,ind);}
		size_t inner_algebra_indices(Face* elem, std::vector<size_t>& ind) const{return m_spMGDD->inner_algebra_indices(elem,ind);}
		size_t inner_algebra_indices(Volume* elem, std::vector<size_t>& ind) const{return m_spMGDD->inner_algebra_indices(elem,ind);}
	/// \}

	protected:
	///	MultiGrid Level DoF Distribution
		SmartPtr<MGDoFDistribution> m_spMGDD;

	///	MultiGrid Subset Handler
		SmartPtr<SurfaceView> m_spSurfView;

	///	Grid level
		GridLevel m_level;
};

} // end namespace ug

#endif /* __H__UG__LIB_DISC__DOF_MANAGER__DOF_DISTRIBUTION__ */