/*
 * communication_policies.h
 *
 *  Created on: 14.6.2010
 *      Author: A. Vogel, S.Reiter
 */

#ifndef __H__LIB_ALGEBRA__PARALLELIZATION__COMMUNICATION_POLICIES__
#define __H__LIB_ALGEBRA__PARALLELIZATION__COMMUNICATION_POLICIES__

#include "parallel_index_layout.h"
#include "pcl/pcl.h"


namespace ug{

// predeclaration of block_traits
template <typename t> struct block_traits;

/**
 * \brief Communication Policies for parallel Algebra
 *
 * Algebra Communication Policies are the basic building blocks for the
 * parallel communication between Vectors and Matrices.
 *
 * \defgroup lib_algebra_parallelization_policies Parallel Algebra Communication Policies
 * \ingroup lib_algebra_parallelization
 */

/// \addtogroup lib_algebra_parallelization_policies
/// @{

/// Communication Policy to copy values of a vector
/**
 * This class is used as a policy to copy values on the interfaces of a
 * parallel vector. The collecting interfaces pack the values on the interface
 * into a stream. The extracting interfaces receive the stream and overwrites
 * the values of the vector with the sent data.
 *
 * \tparam	TVector		Vector type
 */
template <class TVector>
class ComPol_VecCopy : public pcl::ICommunicationPolicy<IndexLayout>
{
	public:
	///	Default constructor
		ComPol_VecCopy() : m_pVec(NULL)	{}

	///	Constructor setting the vector
		ComPol_VecCopy(TVector* pVec) : m_pVec(pVec)	{}

	///	set the vector to work on
		void set_vector(TVector* pVec)	{m_pVec = pVec;}

	/// returns the buffer size
	/**
	 * This function returns the size of the buffer needed for the communication
	 * of passed interface. If the vector has fixed size entries this is just
	 * the number of interface entries times the size of the entry. In case
	 * of a variable size entry type a negative value is returned to indicate
	 * that no buffer size can be determined in advanced.
	 *
	 * \param[in]	interface	Interface that will communicate
	 */
		virtual int
		get_required_buffer_size(Interface& interface)
		{
			if(block_traits<typename TVector::value_type>::is_static)
				return interface.size() * sizeof(typename TVector::value_type);
			else
				return -1;
		}

	///	writes the interface values into a buffer that will be sent
	/**
	 * This function collects all entries of the vector into a buffer that
	 * are part of the interface.
	 *
	 * \param[out]		buff		Buffer
	 * \param[in]		interface	Interface that will communicate
	 */
		virtual bool
		collect(std::ostream& buff, Interface& interface)
		{
		//	check that vector has been set
			if(m_pVec == NULL) return false;

		//	rename for convenience
			TVector& v = *m_pVec;

		//	loop interface
			for(typename Interface::iterator iter = interface.begin();
				iter != interface.end(); ++iter)
			{
			//	get index
				const size_t index = interface.get_element(iter);

			//	write entry into buffer
				BlockSerialize(v[index], buff);
			}
			return true;
		}

	///	writes values from a buffer into the interface
	/**
	 * This function writes the buffer values into the vector.
	 *
	 * \param[out]		buff		Buffer
	 * \param[in]		interface	Interface that communicates
	 */
		virtual bool
		extract(std::istream& buff, Interface& interface)
		{
		//	check that vector has been set
			if(m_pVec == NULL) return false;

		//	rename for convenience
			TVector& v = *m_pVec;

		//	loop interface
			for(typename Interface::iterator iter = interface.begin();
				iter != interface.end(); ++iter)
			{
			//	get index
				const size_t index = interface.get_element(iter);

			//	write entry into vector
				BlockDeserialize(buff, v[index]);
			}
			return true;
		}

	private:
	//	pointer to current vector
		TVector* m_pVec;
};

/// Communication Policy to copy scaled values of a vector
/**
 * This class is used as a policy to copy scaled values on the interfaces of a
 * parallel vector. The collecting interfaces pack the values on the interface
 * into a stream. The extracting interfaces receive the stream and overwrites
 * the values of the vector with the sent data times the scaling.
 *
 * \tparam	TVector		Vector type
 */
template <class TVector>
class ComPol_VecScaleCopy : public pcl::ICommunicationPolicy<IndexLayout>
{
	public:
	///	Default Constructor
		ComPol_VecScaleCopy() : m_pVec(NULL), m_scale(1.0)	{}

	///	Constructor setting vector and scaling factor
		ComPol_VecScaleCopy(TVector* pVec, number scale)
			: m_pVec(pVec), m_scale(scale)	{}

	///	sets the vector that we be used for communication
		void set_vector(TVector* pVec)	{m_pVec = pVec;}

	///	sets the scaling factor
		void set_scale(number scale)	{m_scale = scale;}

	/// returns the buffer size
	/**
	 * This function returns the size of the buffer needed for the communication
	 * of passed interface. If the vector has fixed size entries this is just
	 * the number of interface entries times the size of the entry. In case
	 * of a variable size entry type a negative value is returned to indicate
	 * that no buffer size can be determined in advanced.
	 *
	 * \param[in]	interface	Interface that will communicate
	 */
		virtual int
		get_required_buffer_size(Interface& interface)
		{
			if(block_traits<typename TVector::value_type>::is_static)
				return interface.size() * sizeof(typename TVector::value_type);
			else
				return -1;
		}

	///	writes the interface values into a buffer that will be sent
	/**
	 * This function collects all entries of the vector into a buffer that
	 * are part of the interface.
	 *
	 * \param[out]		buff		Buffer
	 * \param[in]		interface	Interface that will communicate
	 */
		virtual bool
		collect(std::ostream& buff, Interface& interface)
		{
		//	check that vector has been set
			if(m_pVec == NULL) return false;

		//	rename for convenience
			TVector& v = *m_pVec;

		//	loop interface
			for(typename Interface::iterator iter = interface.begin();
				iter != interface.end(); ++iter)
			{
			//	get index
				const size_t index = interface.get_element(iter);

			//	copy value into buffer
				BlockSerialize(v[index], buff);
			}
			return true;
		}

	///	scales values of a buffer and writes the into the interface
	/**
	 * This function writes the scaled buffer values into the vector.
	 *
	 * \param[out]		buff		Buffer
	 * \param[in]		interface	Interface that communicates
	 */
		virtual bool
		extract(std::istream& buff, Interface& interface)
		{
		//	check that vector has been set
			if(m_pVec == NULL) return false;

		//	rename for convenience
			TVector& v = *m_pVec;

		//	loop interface
			for(typename Interface::iterator iter = interface.begin();
				iter != interface.end(); ++iter)
			{
			//	get index
				const size_t index = interface.get_element(iter);

			//	copy value from buffer
				BlockDeserialize(buff, v[index]);

			//	scale value
				v[index] *= m_scale;
			}
			return true;
		}

	private:
		TVector* m_pVec;

		number m_scale;
};

/// Communication Policy to add values of a vector
/**
 * This class is used as a policy to add values on the interfaces of a
 * parallel vector. The collecting interfaces pack the values on the interface
 * into a stream. The extracting interfaces receive the stream and adds
 * the values of the vector.
 *
 * \tparam	TVector		Vector type
 */
template <class TVector>
class ComPol_VecAdd : public pcl::ICommunicationPolicy<IndexLayout>
{
	public:
	///	Default constructor
		ComPol_VecAdd() : m_pVec(NULL)	{}

	///	Constructor setting the values
		ComPol_VecAdd(TVector* pVec) : m_pVec(pVec)	{}

	///	sets the vector used in communication
		void set_vector(TVector* pVec)	{m_pVec = pVec;}

	/// returns the buffer size
	/**
	 * This function returns the size of the buffer needed for the communication
	 * of passed interface. If the vector has fixed size entries this is just
	 * the number of interface entries times the size of the entry. In case
	 * of a variable size entry type a negative value is returned to indicate
	 * that no buffer size can be determined in advanced.
	 *
	 * \param[in]	interface	Interface that will communicate
	 */
		virtual int
		get_required_buffer_size(Interface& interface)
		{
			if(block_traits<typename TVector::value_type>::is_static)
				return interface.size() * sizeof(typename TVector::value_type);
			else
				return -1;
		}

	///	writes the interface values into a buffer that will be sent
	/**
	 * This function collects all entries of the vector into a buffer that
	 * are part of the interface.
	 *
	 * \param[out]		buff		Buffer
	 * \param[in]		interface	Interface that will communicate
	 */
		virtual bool
		collect(std::ostream& buff, Interface& interface)
		{
		//	check that vector has been set
			if(m_pVec == NULL) return false;

		//	rename for convenience
			TVector& v = *m_pVec;

		//	loop interface
			for(typename Interface::iterator iter = interface.begin();
				iter != interface.end(); ++iter)
			{
			//	get index
				const size_t index = interface.get_element(iter);

			// copy entry
				BlockSerialize(v[index], buff);
			}
			return true;
		}

	///	adds values of a buffer to the interface values
	/**
	 * This function adds the buffer values to the vector values.
	 *
	 * \param[out]		buff		Buffer
	 * \param[in]		interface	Interface that communicates
	 */
		virtual bool
		extract(std::istream& buff, Interface& interface)
		{
		//	check that vector has been set
			if(m_pVec == NULL) return false;

		//	rename for convenience
			TVector& v = *m_pVec;

		//	entry
			typename TVector::value_type entry;

		//	loop interface
			for(typename Interface::iterator iter = interface.begin();
				iter != interface.end(); ++iter)
			{
			//	get index
				const size_t index = interface.get_element(iter);

			//	copy entry
				BlockDeserialize(buff, entry);

			//	add entry
				v[index] += entry;
			}
			return true;
		}

	private:
		TVector* m_pVec;
};

/// Communication Policy to add values of a vector
/**
 * This class is used as a policy to add values on the interfaces of a
 * parallel vector. The collecting interfaces pack the values on the interface
 * into a stream. The extracting interfaces receive the stream and adds
 * the values of the vector.
 *
 * \tparam	TVector		Vector type
 */
template <class TVector>
class ComPol_VecScaleAdd : public pcl::ICommunicationPolicy<IndexLayout>
{
	public:
	///	Default constructor
		ComPol_VecScaleAdd() : m_pVec(NULL), m_scale(1.0) {}

	///	Constructor setting the values
		ComPol_VecScaleAdd(TVector* pVec) : m_pVec(pVec)	{}

	///	sets the vector used in communication
		void set_vector(TVector* pVec)	{m_pVec = pVec;}

	///	sets the scaling factor
		void set_scale(number scale)	{m_scale = scale;}

	/// returns the buffer size
	/**
	 * This function returns the size of the buffer needed for the communication
	 * of passed interface. If the vector has fixed size entries this is just
	 * the number of interface entries times the size of the entry. In case
	 * of a variable size entry type a negative value is returned to indicate
	 * that no buffer size can be determined in advanced.
	 *
	 * \param[in]	interface	Interface that will communicate
	 */
		virtual int
		get_required_buffer_size(Interface& interface)
		{
			if(block_traits<typename TVector::value_type>::is_static)
				return interface.size() * sizeof(typename TVector::value_type);
			else
				return -1;
		}

	///	writes the interface values into a buffer that will be sent
	/**
	 * This function collects all entries of the vector into a buffer that
	 * are part of the interface.
	 *
	 * \param[out]		buff		Buffer
	 * \param[in]		interface	Interface that will communicate
	 */
		virtual bool
		collect(std::ostream& buff, Interface& interface)
		{
		//	check that vector has been set
			if(m_pVec == NULL) return false;

		//	rename for convenience
			TVector& v = *m_pVec;

		//	loop interface
			for(typename Interface::iterator iter = interface.begin();
				iter != interface.end(); ++iter)
			{
			//	get index
				const size_t index = interface.get_element(iter);

			// copy entry
				BlockSerialize(v[index], buff);
			}
			return true;
		}

	///	scales and adds values of a buffer to the interface values
	/**
	 * This function sclaes and adds the buffer values to the vector values.
	 *
	 * \param[out]		buff		Buffer
	 * \param[in]		interface	Interface that communicates
	 */
		virtual bool
		extract(std::istream& buff, Interface& interface)
		{
		//	check that vector has been set
			if(m_pVec == NULL) return false;

		//	rename for convenience
			TVector& v = *m_pVec;

		//	entry
			typename TVector::value_type entry;

		//	loop interface
			for(typename Interface::iterator iter = interface.begin();
				iter != interface.end(); ++iter)
			{
			//	get index
				const size_t index = interface.get_element(iter);

			//	copy entry
				BlockDeserialize(buff, entry);

			//	add entry
				v[index] += entry * m_scale;
			}
			return true;
		}

	private:
		TVector* m_pVec;

		number m_scale;
};


/// Communication Policy to add values of a vector and reset value to zero on sending interface
/**
 * This class is used as a policy to add values on the interfaces of a
 * parallel vector and set the values to zero on the sending interface.
 * The collecting interfaces pack the values on the interface and sets them to
 * zero into a stream. The extracting interfaces receive the stream and adds
 * the values to those of the vector.
 *
 * \tparam	TVector		Vector type
 */
template <class TVector>
class ComPol_VecAddSetZero : public pcl::ICommunicationPolicy<IndexLayout>
{
	public:
	///	Default Constructor
		ComPol_VecAddSetZero() : m_pVec(NULL)	{}

	///	Constructor setting vector
		ComPol_VecAddSetZero(TVector* pVec) : m_pVec(pVec)	{}

	///	sets the vector that we be used for communication
		void set_vector(TVector* pVec)	{m_pVec = pVec;}

	/// returns the buffer size
	/**
	 * This function returns the size of the buffer needed for the communication
	 * of passed interface. If the vector has fixed size entries this is just
	 * the number of interface entries times the size of the entry. In case
	 * of a variable size entry type a negative value is returned to indicate
	 * that no buffer size can be determined in advanced.
	 *
	 * \param[in]	interface	Interface that will communicate
	 */
		virtual int
		get_required_buffer_size(Interface& interface)
		{
			if(block_traits<typename TVector::value_type>::is_static)
				return interface.size() * sizeof(typename TVector::value_type);
			else
				return -1;
		}

	///	writes the interface values into a buffer that will be sent and then sets
	/// the value to zero on the interface
	/**
	 * This function collects all entries of the vector into a buffer that
	 * are part of the interface. The values on the interface are then set to
	 * zero.
	 *
	 * \param[out]		buff		Buffer
	 * \param[in]		interface	Interface that will communicate
	 */
		virtual bool
		collect(std::ostream& buff, Interface& interface)
		{
		//	check that vector has been set
			if(m_pVec == NULL) return false;

		//	rename for convenience
			TVector& v = *m_pVec;

		//	loop interface
			for(typename Interface::iterator iter = interface.begin();
				iter != interface.end(); ++iter)
			{
			//	get index
				const size_t index = interface.get_element(iter);

			// copy values
				BlockSerialize(v[index], buff);

			// set to zero on this process
				v[index] = 0.0;
			}
			return true;
		}

	///	adds the values of a buffer to the values on the interface
	/**
	 * This function adds the values of the buffer to the interface values.
	 *
	 * \param[out]		buff		Buffer
	 * \param[in]		interface	Interface that communicates
	 */
		virtual bool
		extract(std::istream& buff, Interface& interface)
		{
		//	check that vector has been set
			if(m_pVec == NULL) return false;

		//	rename for convenience
			TVector& v = *m_pVec;

		//	entry
			typename TVector::value_type entry;

		//	loop interface
			for(typename Interface::iterator iter = interface.begin();
				iter != interface.end(); ++iter)
			{
			//	get index
				const size_t index = interface.get_element(iter);

			//	copy values
				BlockDeserialize(buff, entry);

			//	add entry
				v[index] += entry;
			}
			return true;
		}

	private:
		TVector* m_pVec;
};

/// Communication Policy to subtract values of a vector
/**
 * This class is used as a policy to subtract values on the interfaces of a
 * parallel vector. The collecting interfaces pack the values on the interface
 * into a stream. The extracting interfaces receive the stream and subtracts
 * the values of the vector.
 *
 * \tparam	TVector		Vector type
 */
template <class TVector>
class ComPol_VecSubtract : public pcl::ICommunicationPolicy<IndexLayout>
{
	public:
	///	Default constructor
		ComPol_VecSubtract() : m_pVec(NULL)	{}

	///	Constructor setting the values
		ComPol_VecSubtract(TVector* pVec) : m_pVec(pVec)	{}

	///	sets the vector used in communication
		void set_vector(TVector* pVec)	{m_pVec = pVec;}

	/// returns the buffer size
	/**
	 * This function returns the size of the buffer needed for the communication
	 * of passed interface. If the vector has fixed size entries this is just
	 * the number of interface entries times the size of the entry. In case
	 * of a variable size entry type a negative value is returned to indicate
	 * that no buffer size can be determined in advanced.
	 *
	 * \param[in]	interface	Interface that will communicate
	 */
		virtual int
		get_required_buffer_size(Interface& interface)
		{
			if(block_traits<typename TVector::value_type>::is_static)
				return interface.size() * sizeof(typename TVector::value_type);
			else
				return -1;
		}

	///	writes the interface values into a buffer that will be sent
	/**
	 * This function collects all entries of the vector into a buffer that
	 * are part of the interface.
	 *
	 * \param[out]		buff		Buffer
	 * \param[in]		interface	Interface that will communicate
	 */
		virtual bool
		collect(std::ostream& buff, Interface& interface)
		{
		//	check that vector has been set
			if(m_pVec == NULL) return false;

		//	rename for convenience
			TVector& v = *m_pVec;

		//	loop interface
			for(typename Interface::iterator iter = interface.begin();
				iter != interface.end(); ++iter)
			{
			// get index
				const size_t index = interface.get_element(iter);

			// copy value
				BlockSerialize(v[index], buff);
			}
			return true;
		}

	///	subtracts values of a buffer to the interface values
	/**
	 * This function subtracts the buffer values to the vector values.
	 *
	 * \param[out]		buff		Buffer
	 * \param[in]		interface	Interface that communicates
	 */
		virtual bool
		extract(std::istream& buff, Interface& interface)
		{
		//	check that vector has been set
			if(m_pVec == NULL) return false;

		//	rename for convenience
			TVector& v = *m_pVec;

		// entry
			typename TVector::value_type entry;

		//	loop interface
			for(typename Interface::iterator iter = interface.begin();
				iter != interface.end(); ++iter)
			{
			//	get index
				const size_t index = interface.get_element(iter);

			//	copy vector
				BlockDeserialize(buff, entry);

			//	subtract entry
				v[index] -= entry;
			}
			return true;
		}

	private:
		TVector* m_pVec;
};

/// @}

}//	end of namespace


#endif /* __H__LIB_ALGEBRA__PARALLELIZATION__COMMUNICATION_POLICIES__ */
