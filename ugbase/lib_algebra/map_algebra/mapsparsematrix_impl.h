/*
 *  mapsparsematrix_impl.hpp
 *
 *  Created by Martin Rupp on 05.11.2012
 *  2012 G-CSC
 *
 */
#ifndef __H__UG__MAP_ALGEBRA__SPARSEMATRIX_IMPL__
#define __H__UG__MAP_ALGEBRA__SPARSEMATRIX_IMPL__

// creation etc

#include <fstream>
#include <cstring>

#include "lib_algebra/common/operations_vec.h"
#include "common/profiler/profiler.h"
#include "mapsparsematrix.h"
#include <vector>
#include <algorithm>



namespace ug{

template<typename T>
MapSparseMatrix<T>::MapSparseMatrix()
{
}

template<typename T>
bool MapSparseMatrix<T>::resize(size_t newRows, size_t newCols)
{
	PROFILE_BEGIN_GROUP(MapSparseMatrix_resize, "algebra MapSparseMatrix");
	data.resize(newRows);
	cols = newCols;
	return true;
}


template<typename T>
bool MapSparseMatrix<T>::set_as_transpose_of(const SparseMatrix<value_type> &B, double scale)
{
	PROFILE_BEGIN_GROUP(MapSparseMatrix_set_as_transpose_of, "algebra MapSparseMatrix");
	resize(B.num_cols(), B.num_rows());

	for(size_t r=0; r<B.num_rows(); r++)
		for(const_row_iterator it = B.begin_row(r); it != B.end_row(r); ++it)
			operator()(it.index(), r) = scale*it.value();
	return true;
}

template<typename T>
template<typename vector_t>
inline void MapSparseMatrix<T>::mat_mult_add_row(size_t row, typename vector_t::value_type &dest, double alpha, const vector_t &v) const
{
	for(const_row_iterator conn = begin_row(row); conn != end_row(row); ++conn)
		MatMultAdd(dest, 1.0, dest, alpha, conn.value(), v[conn.index()]);
}


// calculate dest = alpha1*v1 + beta1*A*w1 (A = this matrix)
template<typename T>
template<typename vector_t>
bool MapSparseMatrix<T>::axpy(vector_t &dest,
		const number &alpha1, const vector_t &v1,
		const number &beta1, const vector_t &w1) const
{
	PROFILE_BEGIN_GROUP(SparseMatrix_axpy, "algebra MapSparseMatrix");
	if(alpha1 == 0.0)
	{
		for(size_t i=0; i < num_rows(); i++)
		{
			const_row_iterator conn = begin_row(i);
			if(conn  == end_row(i)) continue;
			MatMult(dest[i], beta1, conn.value(), w1[conn.index()]);
			for(++conn; conn != end_row(i); ++conn)
				// res[i] += conn.value() * x[conn.index()];
				MatMultAdd(dest[i], 1.0, dest[i], beta1, conn.value(), w1[conn.index()]);
		}
	}
	else if(&dest == &v1)
	{
		if(alpha1 != 1.0) {
			for(size_t i=0; i < num_rows(); i++)
			{
				dest[i] *= alpha1;
				mat_mult_add_row(i, dest[i], beta1, w1);
			}
		}
		else
			for(size_t i=0; i < num_rows(); i++)
				mat_mult_add_row(i, dest[i], beta1, w1);

	}
	else
	{
		for(size_t i=0; i < num_rows(); i++)
		{
			VecScaleAssign(dest[i], alpha1, v1[i]);
			mat_mult_add_row(i, dest[i], beta1, w1);
		}
	}
	return true;
}

// calculate dest = alpha1*v1 + beta1*A^T*w1 (A = this matrix)
template<typename T>
template<typename vector_t>
bool MapSparseMatrix<T>::axpy_transposed(vector_t &dest,
		const number &alpha1, const vector_t &v1,
		const number &beta1, const vector_t &w1) const
{
	PROFILE_BEGIN_GROUP(MapSparseMatrix_axpy_transposed, "algebra MapSparseMatrix");
	
	if(&dest == &v1) {
		if(alpha1 == 0.0)
			dest.set(0.0);
		else if(alpha1 != 1.0)
			dest *= alpha1;
	}
	else if(alpha1 == 0.0)
		dest.set(0.0);
	else
		VecScaleAssign(dest, alpha1, v1);

	for(size_t i=0; i<num_rows(); i++)
	{
		for(const_row_iterator conn = begin_row(i); conn != end_row(i); ++conn)
		{
			if(conn.value() != 0.0)
				// dest[conn.index()] += beta1 * conn.value() * w1[i];
				MatMultTransposedAdd(dest[conn.index()], 1.0, dest[conn.index()], beta1, conn.value(), w1[i]);
		}
	}
	return true;
}

template<typename T>
bool MapSparseMatrix<T>::set(double a)
{
	PROFILE_BEGIN_GROUP(MapSparseMatrix_set, "algebra MapSparseMatrix");
	
	for(size_t row=0; row<num_rows(); row++)
		for(row_iterator it = begin_row(row); it != end_row(row); ++it)
		{
			if(it.index() == row)
				it.value() = a;
			else
				it.value() = 0.0;
		}
	return true;
}


template<typename T>
inline bool MapSparseMatrix<T>::is_isolated(size_t i) const
{
	UG_ASSERT(i < num_rows() && i >= 0, *this << ": " << i << " out of bounds.");

	for(const_row_iterator it = begin_row(i); it != end_row(i); ++it)
		if(it.index() != i && it.value() != 0.0)
			return false;
	return true;
}


template<typename T>
void MapSparseMatrix<T>::set_matrix_row(size_t row, connection *c, size_t nr)
{
	PROFILE_BEGIN_GROUP(MapSparseMatrix_set_matrix_row, "algebra MapSparseMatrix");
	for(size_t i=0; i<nr; i++)
		operator()(row, c[i].iIndex) = c[i].dValue;
}

template<typename T>
void MapSparseMatrix<T>::add_matrix_row(size_t row, connection *c, size_t nr)
{
	PROFILE_BEGIN_GROUP(MapSparseMatrix_add_matrix_row, "algebra MapSparseMatrix");
	for(size_t i=0; i<nr; i++)
		operator()(row, c[i].iIndex) += c[i].dValue;
}


template<typename T>
bool MapSparseMatrix<T>::set_as_copy_of(const MapSparseMatrix<T> &B, double scale)
{
	PROFILE_BEGIN_GROUP(MapSparseMatrix_set_as_copy_of, "algebra MapSparseMatrix");
	resize(B.num_rows(), B.num_cols());
	for(size_t i=0; i < B.num_rows(); i++)
		for(const_row_iterator it = B.begin_row(i); it != B.end_row(i); ++it)
			operator()(i, it.index()) = scale*it.value();
	return true;
}



template<typename T>
bool MapSparseMatrix<T>::scale(double d)
{
	PROFILE_BEGIN_GROUP(MapSparseMatrix_scale, "algebra MapSparseMatrix");
	for(size_t i=0; i < num_rows(); i++)
		for(row_iterator it = begin_row(i); it != end_row(i); ++it)
			it.value() *= d;
	return true;
}







} // namespace ug

#endif
