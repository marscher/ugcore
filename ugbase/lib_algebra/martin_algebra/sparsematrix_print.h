/*
 *  sparsematrix_print.hpp
 *  flexamg
 *
 *  Created by Martin Rupp on 04.11.09.
 *  Copyright 2009 G-CSC, University of Frankfurt. All rights reserved.
 *
 */
#ifndef __H__UG__MARTIN_ALGEBRA__SPARSEMATRIX_PRINT__
#define  __H__UG__MARTIN_ALGEBRA__SPARSEMATRIX_PRINT__

namespace ug {

//!
//! print to console whole SparseMatrix
template<typename T>
void SparseMatrix<T>::print(const char * const text) const
{
	cout << "================= SparseMatrix " << rows << "x" << cols << " =================" << endl;
	for(size_t i=0; i < rows; i++)
		getrow(i).print();
}


//!
//! print the row row to the console
template<typename T>
void SparseMatrix<T>::printrow(size_t row) const
{
#ifdef FLEXAMG
	cout << "row " << row << " [" << GetOriginalIndex(tolevel, row) << "] : ";
#else
	cout << "row " << row << ": ";
#endif
	for(cRowIterator it=beginRow(row); !it.isEnd(); ++it)
	{
		if((*it).dValue == 0.0) continue;
		cout << " ";
#ifdef FLEXAMG
		cout << "(" << (*it).iIndex << " [" << GetOriginalIndex(fromlevel, (*it).iIndex) << "] -> " << (*it).dValue << ")";
#else
		cout << "(" << (*it).iIndex << " -> " << (*it).dValue << ")";
#endif
	}
	cout << endl;
}

template<typename T>
void SparseMatrix<T>::printtype() const
{
	cout << *this;
}



#define CONNECTION_VIEWER_VERSION 1

// WriteMatrixToConnectionViewer
//--------------------------------------------------
//! writes to a file in somewhat SparseMatrix-market format (for connection viewer)
template<typename Matrix_type, typename postype>
void WriteMatrixToConnectionViewer(const char *filename, const Matrix_type &A, postype *positions, int dimensions)
{
	fstream file(filename, ios::out);
	file << CONNECTION_VIEWER_VERSION << endl;
	file << dimensions << endl;

	int rows = A.num_rows();
	// write positions
	file << rows << endl;
	if(dimensions == 2)
		for(int i=0; i < rows; i++)
			file << positions[i][0] << " " << positions[i][1] << endl;
	else
		for(int i=0; i < rows; i++)
			file << positions[i][0] << " " << positions[i][1] << positions[i][2] << endl;

	file << 1 << endl; // show all cons
	// write connections
	for(int i=0; i < rows; i++)
	{
		for(typename Matrix_type::cRowIterator conn = A.beginRow(i); !conn.isEnd(); ++conn)
			if((*conn).dValue != 0.0)
				file << i << " " << (*conn).iIndex << " " << (*conn).dValue <<		endl;
	}
}

/*template<>
template<typename Matrix_type>
void WriteMatrixToConnectionViewer(const char *filename, const Matrix_type A, MathVector<2> *positions, int dimensions)
{
	WriteMatrixToConnectionViewer(filename, A, positions, 2);
}
template<>
template<typename Matrix_type>
void WriteMatrixToConnectionViewer(const char *filename, const Matrix_type A, MathVector<3> *positions, int dimensions)
{
	WriteMatrixToConnectionViewer(filename, A, positions, 3);
}*/

}
#endif // __H__UG__MARTIN_ALGEBRA__SPARSEMATRIX_PRINT__
