# ifndef CPPAD_LOCAL_POD_VECTOR_HPP
# define CPPAD_LOCAL_POD_VECTOR_HPP

/* --------------------------------------------------------------------------
CppAD: C++ Algorithmic Differentiation: Copyright (C) 2003-17 Bradley M. Bell

CppAD is distributed under multiple licenses. This distribution is under
the terms of the
                    Eclipse Public License Version 1.0.

A copy of this license is included in the COPYING file of this distribution.
Please visit http://www.coin-or.org/CppAD/ for information on other licenses.
-------------------------------------------------------------------------- */

# if CPPAD_CSTDINT_HAS_8_TO_64
# include <cstdint>
# endif
# include <algorithm>
# include <cppad/utility/thread_alloc.hpp>
# include <cppad/core/cppad_assert.hpp>
# include <cppad/local/is_pod.hpp>

namespace CppAD { namespace local { // BEGIN_CPPAD_LOCAL_NAMESPACE
/*!
\file pod_vector.hpp
File used to define pod_vector class
*/
// ---------------------------------------------------------------------------
/*!
A vector class with Type element that does not use element constructors
or destructors when Type is Plain Old Data (pod).
*/
template <class Type>
class pod_vector {
private:
	/// number of elements currently in this vector
	size_t length_;
	//
	/// maximum number of Type elements current allocation can hold
	size_t capacity_;
	//
	/// pointer to the first type elements
	/// (not defined and should not be used when capacity_ = 0)
	Type   *data_;
	//
	/// do not use the copy constructor
	explicit pod_vector(const pod_vector& )
	{	CPPAD_ASSERT_UNKNOWN(false); }
public:
	/// default constructor sets capacity_ = length_ = data_ = 0
	pod_vector(void)
	: length_(0), capacity_(0), data_(CPPAD_NULL)
	{	CPPAD_ASSERT_UNKNOWN( is_pod<size_t>() );
	}
	/// sizing constructor
	pod_vector(
		/// number of elements in this vector
		size_t n
	)   : length_(0), capacity_(0), data_(CPPAD_NULL)
	{	extend(n); }
	// ----------------------------------------------------------------------
	/// Destructor: returns allocated memory to \c thread_alloc;
	/// see \c extend.  If this is not plain old data,
	/// the destructor for each element is called.
	~pod_vector(void)
	{	if( capacity_ > 0 )
		{	void* v_ptr = reinterpret_cast<void*>( data_ );
			if( ! is_pod<Type>() )
			{	// call destructor for each element
				size_t i;
				for(i = 0; i < capacity_; i++)
					(data_ + i)->~Type();
			}
			thread_alloc::return_memory(v_ptr);
		}
	}
	// ----------------------------------------------------------------------
	/// current number of elements in this vector.
	size_t size(void) const
	{	return length_; }
	//
	/// current capacity (amount of allocated storage) for this vector.
	size_t capacity(void) const
	{	return capacity_; }
	//
	/// current data pointer is no longer valid after any of the following:
	/// extend, erase, operator=, and ~pod_vector.
	/// Take extreem care when using this function.
	Type* data(void)
	{	return data_; }
	//
	/// const version of data pointer (see non-const documentation)
	const Type* data(void) const
	{	return data_; }
	// ----------------------------------------------------------------------
	/*!
	Increase the number of elements the end of this vector
	(existing elements are always preserved).

	\param n
	is the number of elements to add to end of this vector.

	\return
	is the number  of elements in the vector before \c extend was extended.
	This is the index of the first new element added to the vector.

	- If \c Type is plain old data, new elements are not initialized;
	i.e., their constructor is not called. Otherwise, the constructor
	is called for each new element.

	- This and resize are the only routine that allocate memory for
	\c pod_vector. They uses thread_alloc for this allocation, hence this
	determines which thread corresponds to this vector (when in parallel mode).
	*/
	size_t extend(size_t n)
	{	size_t old_length   = length_;
		length_            += n;

		// check if we can use current memory
		if( capacity_ >= length_ )
			return old_length;

		// save more old information
		size_t old_capacity = capacity_;
		Type* old_data      = data_;

		// get new memory and set capacity
		size_t length_bytes = length_ * sizeof(Type);
		size_t capacity_bytes;
		void* v_ptr = thread_alloc::get_memory(length_bytes, capacity_bytes);
		capacity_   = capacity_bytes / sizeof(Type);
		data_       = reinterpret_cast<Type*>(v_ptr);
		CPPAD_ASSERT_UNKNOWN( length_ <= capacity_ );

		size_t i;
		if( ! is_pod<Type>() )
		{	// call constructor for each new element
			for(i = 0; i < capacity_; i++)
				new(data_ + i) Type();
		}

		// copy old data to new data
		for(i = 0; i < old_length; i++)
			data_[i] = old_data[i];

		// return old memory to available pool
		if( old_capacity > 0 )
		{	v_ptr = reinterpret_cast<void*>( old_data );
			if( ! is_pod<Type>() )
			{	for(i = 0; i < old_capacity; i++)
					(old_data + i)->~Type();
			}
			thread_alloc::return_memory(v_ptr);
		}

		// return value for extend(n) is the old length
		return old_length;
	}
	// ----------------------------------------------------------------------
	/*!
	resize the vector (existing elements preserved when n <= capacity_).

	\param n
	is the new size for this vector.

	\par
	if n <= capacity(), no memory is freed or allocated, the capacity
	is not changed, and existing elements are preserved.
	If n > capacity(), new memory is allocates and all the
	data in the vector is lost.

	- If \c Type is plain old data, new elements are not initialized;
	i.e., their constructor is not called. Otherwise, the constructor
	is called for each new element.

	- This and extend are the only routine that allocate memory for
	\c pod_vector. They uses thread_alloc for this allocation, hence this
	determines which thread corresponds to this vector (when in parallel mode).
	*/
	void resize(size_t n)
	{	length_ = n;

		// check if we must allocate new memory
		if( capacity_ < length_ )
		{	void* v_ptr;
			//
			// return old memory to available pool
			if( capacity_ > 0 )
			{	v_ptr = reinterpret_cast<void*>( data_ );
				if( ! is_pod<Type>() )
				{	// call destructor for each old element
					for(size_t i = 0; i < capacity_; i++)
						(data_ + i)->~Type();
				}
				thread_alloc::return_memory(v_ptr);
			}
			//
			// get new memory and set capacity
			size_t length_bytes = length_ * sizeof(Type);
			size_t capacity_bytes;
			v_ptr     = thread_alloc::get_memory(length_bytes, capacity_bytes);
			capacity_ = capacity_bytes / sizeof(Type);
			data_     = reinterpret_cast<Type*>(v_ptr);
			//
			CPPAD_ASSERT_UNKNOWN( length_ <= capacity_ );
			//
			if( ! is_pod<Type>() )
			{	// call constructor for each new element
				for(size_t i = 0; i < capacity_; i++)
					new(data_ + i) Type();
			}
		}
	}
	// ----------------------------------------------------------------------
	/// non-constant element access; i.e., we can change this element value
	Type& operator[](
		/// element index, must be less than length
		size_t i
	)
	{	CPPAD_ASSERT_UNKNOWN( i < length_ );
		return data_[i];
	}
	// ----------------------------------------------------------------------
	/// constant element access; i.e., we cannot change this element value
	const Type& operator[](
		/// element index, must be less than length
		size_t i
	) const
	{	CPPAD_ASSERT_UNKNOWN( i < length_ );
		return data_[i];
	}
	// ----------------------------------------------------------------------
	/*!
	Remove all the elements from this vector but leave the capacity
	and data pointer as is.

	*/
	void erase(void)
	{	length_ = 0;
		return;
	}
	// ----------------------------------------------------------------------
	/*!
	Remove all the elements from this vector and free its memory.
	*/
	void clear(void)
	{	if( capacity_ > 0 )
		{	void* v_ptr = reinterpret_cast<void*>( data_ );
			if( ! is_pod<Type>() )
			{	// call destructor for each element
				size_t i;
				for(i = 0; i < capacity_; i++)
					(data_ + i)->~Type();
			}
			thread_alloc::return_memory(v_ptr);
		}
		data_     = CPPAD_NULL;
		capacity_ = 0;
		length_   = 0;
	}
	// -----------------------------------------------------------------------
	/// vector assignment operator
	void operator=(
		/// right hand size of the assingment operation
		const pod_vector& x
	)
	{	size_t i;

		if( x.length_ <= capacity_ )
		{	// use existing allocation for this vector
			length_ = x.length_;
		}
		else
		{	// free old memory and get new memory of sufficient length
			if( capacity_ > 0 )
			{	void* v_ptr = reinterpret_cast<void*>( data_ );
				if( ! is_pod<Type>() )
				{	// call destructor for each element
					for(i = 0; i < capacity_; i++)
						(data_ + i)->~Type();
				}
				thread_alloc::return_memory(v_ptr);
			}
			length_ = capacity_ = 0;
			extend( x.length_ );
		}
		CPPAD_ASSERT_UNKNOWN( length_   == x.length_ );
		for(i = 0; i < length_; i++)
		{	data_[i] = x.data_[i]; }
	}
	// -----------------------------------------------------------------------
	/*!
	Swap all properties of this vector with another.
	This is useful when moving a vector that grows after it has reached
	its final size (without copying every element).

	\param other
	is the other vector that we are swapping this vector with.
	*/
	void swap(pod_vector& other)
	{	std::swap(capacity_, other.capacity_);
		std::swap(length_,   other.length_);
		std::swap(data_,     other.data_);
	}
	// ------------------------------------------------------------------------
	/*!
	Add an element to theh back of this vector

	\param e
	is the element we are adding to the back of the vector.
	*/
	void push_back(const Type& e)
	{	size_t i = extend(1);
		data_[i] = e;
	}
};

} } // END_CPPAD_LOCAL_NAMESPACE
# endif
