// This file is part of Eigen, a lightweight C++ template library
// for linear algebra.
//
// Copyright (C) 2011 Benoit Jacob <jacob.benoit.1@gmail.com>
// Copyright (C) 2011-2013 Gael Guennebaud <gael.guennebaud@inria.fr>
// Copyright (C) 2011-2012 Jitse Niesen <jitse@maths.leeds.ac.uk>
//
// This Source Code Form is subject to the terms of the Mozilla
// Public License v. 2.0. If a copy of the MPL was not distributed
// with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef EIGEN_ASSIGN_EVALUATOR_H
#define EIGEN_ASSIGN_EVALUATOR_H

namespace Eigen {

// This implementation is based on Assign.h

namespace internal {
  
/***************************************************************************
* Part 1 : the logic deciding a strategy for traversal and unrolling       *
***************************************************************************/

// copy_using_evaluator_traits is based on assign_traits

template <typename Derived, typename OtherDerived>
struct copy_using_evaluator_traits
{
public:
  enum {
    DstIsAligned = Derived::Flags & AlignedBit,
    DstHasDirectAccess = Derived::Flags & DirectAccessBit,
    SrcIsAligned = OtherDerived::Flags & AlignedBit,
    JointAlignment = bool(DstIsAligned) && bool(SrcIsAligned) ? Aligned : Unaligned,
    SrcEvalBeforeAssign = (evaluator_traits<OtherDerived>::HasEvalTo == 1)
  };

private:
  enum {
    InnerSize = int(Derived::IsVectorAtCompileTime) ? int(Derived::SizeAtCompileTime)
              : int(Derived::Flags)&RowMajorBit ? int(Derived::ColsAtCompileTime)
              : int(Derived::RowsAtCompileTime),
    InnerMaxSize = int(Derived::IsVectorAtCompileTime) ? int(Derived::MaxSizeAtCompileTime)
              : int(Derived::Flags)&RowMajorBit ? int(Derived::MaxColsAtCompileTime)
              : int(Derived::MaxRowsAtCompileTime),
    MaxSizeAtCompileTime = Derived::SizeAtCompileTime,
    PacketSize = packet_traits<typename Derived::Scalar>::size
  };

  enum {
    StorageOrdersAgree = (int(Derived::IsRowMajor) == int(OtherDerived::IsRowMajor)),
    MightVectorize = StorageOrdersAgree
                  && (int(Derived::Flags) & int(OtherDerived::Flags) & ActualPacketAccessBit),
    MayInnerVectorize  = MightVectorize && int(InnerSize)!=Dynamic && int(InnerSize)%int(PacketSize)==0
                       && int(DstIsAligned) && int(SrcIsAligned),
    MayLinearize = StorageOrdersAgree && (int(Derived::Flags) & int(OtherDerived::Flags) & LinearAccessBit),
    MayLinearVectorize = MightVectorize && MayLinearize && DstHasDirectAccess
                       && (DstIsAligned || MaxSizeAtCompileTime == Dynamic),
      /* If the destination isn't aligned, we have to do runtime checks and we don't unroll,
         so it's only good for large enough sizes. */
    MaySliceVectorize  = MightVectorize && DstHasDirectAccess
                       && (int(InnerMaxSize)==Dynamic || int(InnerMaxSize)>=3*PacketSize)
      /* slice vectorization can be slow, so we only want it if the slices are big, which is
         indicated by InnerMaxSize rather than InnerSize, think of the case of a dynamic block
         in a fixed-size matrix */
  };

public:
  enum {
    Traversal = int(SrcEvalBeforeAssign) ? int(AllAtOnceTraversal) 
              : int(MayInnerVectorize)   ? int(InnerVectorizedTraversal)
              : int(MayLinearVectorize)  ? int(LinearVectorizedTraversal)
              : int(MaySliceVectorize)   ? int(SliceVectorizedTraversal)
              : int(MayLinearize)        ? int(LinearTraversal)
                                         : int(DefaultTraversal),
    Vectorized = int(Traversal) == InnerVectorizedTraversal
              || int(Traversal) == LinearVectorizedTraversal
              || int(Traversal) == SliceVectorizedTraversal
  };

private:
  enum {
    UnrollingLimit      = EIGEN_UNROLLING_LIMIT * (Vectorized ? int(PacketSize) : 1),
    MayUnrollCompletely = int(Derived::SizeAtCompileTime) != Dynamic
                       && int(OtherDerived::CoeffReadCost) != Dynamic
                       && int(Derived::SizeAtCompileTime) * int(OtherDerived::CoeffReadCost) <= int(UnrollingLimit),
    MayUnrollInner      = int(InnerSize) != Dynamic
                       && int(OtherDerived::CoeffReadCost) != Dynamic
                       && int(InnerSize) * int(OtherDerived::CoeffReadCost) <= int(UnrollingLimit)
  };

public:
  enum {
    Unrolling = (int(Traversal) == int(InnerVectorizedTraversal) || int(Traversal) == int(DefaultTraversal))
                ? (
                    int(MayUnrollCompletely) ? int(CompleteUnrolling)
                  : int(MayUnrollInner)      ? int(InnerUnrolling)
                                             : int(NoUnrolling)
                  )
              : int(Traversal) == int(LinearVectorizedTraversal)
                ? ( bool(MayUnrollCompletely) && bool(DstIsAligned) ? int(CompleteUnrolling) 
                                                                    : int(NoUnrolling) )
              : int(Traversal) == int(LinearTraversal)
                ? ( bool(MayUnrollCompletely) ? int(CompleteUnrolling) 
                                              : int(NoUnrolling) )
              : int(NoUnrolling)
  };

#ifdef EIGEN_DEBUG_ASSIGN
  static void debug()
  {
    EIGEN_DEBUG_VAR(DstIsAligned)
    EIGEN_DEBUG_VAR(SrcIsAligned)
    EIGEN_DEBUG_VAR(JointAlignment)
    EIGEN_DEBUG_VAR(InnerSize)
    EIGEN_DEBUG_VAR(InnerMaxSize)
    EIGEN_DEBUG_VAR(PacketSize)
    EIGEN_DEBUG_VAR(StorageOrdersAgree)
    EIGEN_DEBUG_VAR(MightVectorize)
    EIGEN_DEBUG_VAR(MayLinearize)
    EIGEN_DEBUG_VAR(MayInnerVectorize)
    EIGEN_DEBUG_VAR(MayLinearVectorize)
    EIGEN_DEBUG_VAR(MaySliceVectorize)
    EIGEN_DEBUG_VAR(Traversal)
    EIGEN_DEBUG_VAR(UnrollingLimit)
    EIGEN_DEBUG_VAR(MayUnrollCompletely)
    EIGEN_DEBUG_VAR(MayUnrollInner)
    EIGEN_DEBUG_VAR(Unrolling)
  }
#endif
};

/***************************************************************************
* Part 2 : meta-unrollers
***************************************************************************/

/************************
*** Default traversal ***
************************/

template<typename Kernel, int Index, int Stop>
struct copy_using_evaluator_DefaultTraversal_CompleteUnrolling
{
  typedef typename Kernel::DstEvaluatorType DstEvaluatorType;
  typedef typename DstEvaluatorType::XprType DstXprType;
  
  enum {
    outer = Index / DstXprType::InnerSizeAtCompileTime,
    inner = Index % DstXprType::InnerSizeAtCompileTime
  };

  static EIGEN_STRONG_INLINE void run(Kernel &kernel)
  {
    kernel.assignCoeffByOuterInner(outer, inner);
    copy_using_evaluator_DefaultTraversal_CompleteUnrolling<Kernel, Index+1, Stop>::run(kernel);
  }
};

template<typename Kernel, int Stop>
struct copy_using_evaluator_DefaultTraversal_CompleteUnrolling<Kernel, Stop, Stop>
{
  static EIGEN_STRONG_INLINE void run(Kernel&) { }
};

template<typename Kernel, int Index, int Stop>
struct copy_using_evaluator_DefaultTraversal_InnerUnrolling
{
  static EIGEN_STRONG_INLINE void run(Kernel &kernel, int outer)
  {
    kernel.assignCoeffByOuterInner(outer, Index);
    copy_using_evaluator_DefaultTraversal_InnerUnrolling<Kernel, Index+1, Stop>::run(kernel, outer);
  }
};

template<typename Kernel, int Stop>
struct copy_using_evaluator_DefaultTraversal_InnerUnrolling<Kernel, Stop, Stop>
{
  static EIGEN_STRONG_INLINE void run(Kernel&, int) { }
};

/***********************
*** Linear traversal ***
***********************/

template<typename Kernel, int Index, int Stop>
struct copy_using_evaluator_LinearTraversal_CompleteUnrolling
{
  static EIGEN_STRONG_INLINE void run(Kernel& kernel)
  {
    kernel.assignCoeff(Index);
    copy_using_evaluator_LinearTraversal_CompleteUnrolling<Kernel, Index+1, Stop>::run(kernel);
  }
};

template<typename Kernel, int Stop>
struct copy_using_evaluator_LinearTraversal_CompleteUnrolling<Kernel, Stop, Stop>
{
  static EIGEN_STRONG_INLINE void run(Kernel&) { }
};

/**************************
*** Inner vectorization ***
**************************/

template<typename Kernel, int Index, int Stop>
struct copy_using_evaluator_innervec_CompleteUnrolling
{
  typedef typename Kernel::DstEvaluatorType DstEvaluatorType;
  typedef typename DstEvaluatorType::XprType DstXprType;

  enum {
    outer = Index / DstXprType::InnerSizeAtCompileTime,
    inner = Index % DstXprType::InnerSizeAtCompileTime,
    JointAlignment = Kernel::AssignmentTraits::JointAlignment
  };

  static EIGEN_STRONG_INLINE void run(Kernel &kernel)
  {
    kernel.template assignPacketByOuterInner<Aligned, JointAlignment>(outer, inner);
    enum { NextIndex = Index + packet_traits<typename DstXprType::Scalar>::size };
    copy_using_evaluator_innervec_CompleteUnrolling<Kernel, NextIndex, Stop>::run(kernel);
  }
};

template<typename Kernel, int Stop>
struct copy_using_evaluator_innervec_CompleteUnrolling<Kernel, Stop, Stop>
{
  static EIGEN_STRONG_INLINE void run(Kernel&) { }
};

template<typename Kernel, int Index, int Stop>
struct copy_using_evaluator_innervec_InnerUnrolling
{
  static EIGEN_STRONG_INLINE void run(Kernel &kernel, int outer)
  {
    kernel.template assignPacketByOuterInner<Aligned, Aligned>(outer, Index);
    typedef typename Kernel::DstEvaluatorType::XprType DstXprType;
    enum { NextIndex = Index + packet_traits<typename DstXprType::Scalar>::size };
    copy_using_evaluator_innervec_InnerUnrolling<Kernel, NextIndex, Stop>::run(kernel, outer);
  }
};

template<typename Kernel, int Stop>
struct copy_using_evaluator_innervec_InnerUnrolling<Kernel, Stop, Stop>
{
  static EIGEN_STRONG_INLINE void run(Kernel &, int) { }
};

/***************************************************************************
* Part 3 : implementation of all cases
***************************************************************************/

// dense_assignment_loop is based on assign_impl

template<typename Kernel,
         int Traversal = Kernel::AssignmentTraits::Traversal,
         int Unrolling = Kernel::AssignmentTraits::Unrolling>
struct dense_assignment_loop;

/************************
*** Default traversal ***
************************/

template<typename Kernel>
struct dense_assignment_loop<Kernel, DefaultTraversal, NoUnrolling>
{
  static void run(Kernel &kernel)
  {
    typedef typename Kernel::Index Index;
    
    for(Index outer = 0; outer < kernel.outerSize(); ++outer) {
      for(Index inner = 0; inner < kernel.innerSize(); ++inner) {
        kernel.assignCoeffByOuterInner(outer, inner);
      }
    }
  }
};

template<typename Kernel>
struct dense_assignment_loop<Kernel, DefaultTraversal, CompleteUnrolling>
{
  static EIGEN_STRONG_INLINE void run(Kernel &kernel)
  {
    typedef typename Kernel::DstEvaluatorType::XprType DstXprType;
    copy_using_evaluator_DefaultTraversal_CompleteUnrolling<Kernel, 0, DstXprType::SizeAtCompileTime>::run(kernel);
  }
};

template<typename Kernel>
struct dense_assignment_loop<Kernel, DefaultTraversal, InnerUnrolling>
{
  typedef typename Kernel::Index Index;
  static EIGEN_STRONG_INLINE void run(Kernel &kernel)
  {
    typedef typename Kernel::DstEvaluatorType::XprType DstXprType;

    const Index outerSize = kernel.outerSize();
    for(Index outer = 0; outer < outerSize; ++outer)
      copy_using_evaluator_DefaultTraversal_InnerUnrolling<Kernel, 0, DstXprType::InnerSizeAtCompileTime>::run(kernel, outer);
  }
};

/***************************
*** Linear vectorization ***
***************************/


// The goal of unaligned_dense_assignment_loop is simply to factorize the handling
// of the non vectorizable beginning and ending parts

template <bool IsAligned = false>
struct unaligned_dense_assignment_loop
{
  // if IsAligned = true, then do nothing
  template <typename Kernel>
  static EIGEN_STRONG_INLINE void run(Kernel&, typename Kernel::Index, typename Kernel::Index) {}
};

template <>
struct unaligned_dense_assignment_loop<false>
{
  // MSVC must not inline this functions. If it does, it fails to optimize the
  // packet access path.
  // FIXME check which version exhibits this issue
#ifdef _MSC_VER
  template <typename Kernel>
  static EIGEN_DONT_INLINE void run(Kernel &kernel,
                                    typename Kernel::Index start,
                                    typename Kernel::Index end)
#else
  template <typename Kernel>
  static EIGEN_STRONG_INLINE void run(Kernel &kernel,
                                      typename Kernel::Index start,
                                      typename Kernel::Index end)
#endif
  {
    for (typename Kernel::Index index = start; index < end; ++index)
      kernel.assignCoeff(index);
  }
};

template<typename Kernel>
struct dense_assignment_loop<Kernel, LinearVectorizedTraversal, NoUnrolling>
{
  static EIGEN_STRONG_INLINE void run(Kernel &kernel)
  {
    typedef typename Kernel::Index Index;

    const Index size = kernel.size();
    typedef packet_traits<typename Kernel::Scalar> PacketTraits;
    enum {
      packetSize = PacketTraits::size,
      dstIsAligned = int(Kernel::AssignmentTraits::DstIsAligned),
      dstAlignment = PacketTraits::AlignedOnScalar ? Aligned : dstIsAligned,
      srcAlignment = Kernel::AssignmentTraits::JointAlignment
    };
    const Index alignedStart = dstIsAligned ? 0 : internal::first_aligned(&kernel.dstEvaluator().coeffRef(0), size);
    const Index alignedEnd = alignedStart + ((size-alignedStart)/packetSize)*packetSize;

    unaligned_dense_assignment_loop<dstIsAligned!=0>::run(kernel, 0, alignedStart);

    for(Index index = alignedStart; index < alignedEnd; index += packetSize)
      kernel.template assignPacket<dstAlignment, srcAlignment>(index);

    unaligned_dense_assignment_loop<>::run(kernel, alignedEnd, size);
  }
};

template<typename Kernel>
struct dense_assignment_loop<Kernel, LinearVectorizedTraversal, CompleteUnrolling>
{
  typedef typename Kernel::Index Index;
  static EIGEN_STRONG_INLINE void run(Kernel &kernel)
  {
    typedef typename Kernel::DstEvaluatorType::XprType DstXprType;
    
    enum { size = DstXprType::SizeAtCompileTime,
           packetSize = packet_traits<typename Kernel::Scalar>::size,
           alignedSize = (size/packetSize)*packetSize };

    copy_using_evaluator_innervec_CompleteUnrolling<Kernel, 0, alignedSize>::run(kernel);
    copy_using_evaluator_DefaultTraversal_CompleteUnrolling<Kernel, alignedSize, size>::run(kernel);
  }
};

/**************************
*** Inner vectorization ***
**************************/

template<typename Kernel>
struct dense_assignment_loop<Kernel, InnerVectorizedTraversal, NoUnrolling>
{
  static inline void run(Kernel &kernel)
  {
    typedef typename Kernel::Index Index;

    const Index innerSize = kernel.innerSize();
    const Index outerSize = kernel.outerSize();
    const Index packetSize = packet_traits<typename Kernel::Scalar>::size;
    for(Index outer = 0; outer < outerSize; ++outer)
      for(Index inner = 0; inner < innerSize; inner+=packetSize)
        kernel.template assignPacketByOuterInner<Aligned, Aligned>(outer, inner);
  }
};

template<typename Kernel>
struct dense_assignment_loop<Kernel, InnerVectorizedTraversal, CompleteUnrolling>
{
  static EIGEN_STRONG_INLINE void run(Kernel &kernel)
  {
    typedef typename Kernel::DstEvaluatorType::XprType DstXprType;
    copy_using_evaluator_innervec_CompleteUnrolling<Kernel, 0, DstXprType::SizeAtCompileTime>::run(kernel);
  }
};

template<typename Kernel>
struct dense_assignment_loop<Kernel, InnerVectorizedTraversal, InnerUnrolling>
{
  typedef typename Kernel::Index Index;
  static EIGEN_STRONG_INLINE void run(Kernel &kernel)
  {
    typedef typename Kernel::DstEvaluatorType::XprType DstXprType;
    const Index outerSize = kernel.outerSize();
    for(Index outer = 0; outer < outerSize; ++outer)
      copy_using_evaluator_innervec_InnerUnrolling<Kernel, 0, DstXprType::InnerSizeAtCompileTime>::run(kernel, outer);
  }
};

/***********************
*** Linear traversal ***
***********************/

template<typename Kernel>
struct dense_assignment_loop<Kernel, LinearTraversal, NoUnrolling>
{
  static inline void run(Kernel &kernel)
  {
    typedef typename Kernel::Index Index;
    const Index size = kernel.size();
    for(Index i = 0; i < size; ++i)
      kernel.assignCoeff(i);
  }
};

template<typename Kernel>
struct dense_assignment_loop<Kernel, LinearTraversal, CompleteUnrolling>
{
  static EIGEN_STRONG_INLINE void run(Kernel &kernel)
  {
    typedef typename Kernel::DstEvaluatorType::XprType DstXprType;
    copy_using_evaluator_LinearTraversal_CompleteUnrolling<Kernel, 0, DstXprType::SizeAtCompileTime>::run(kernel);
  }
};

/**************************
*** Slice vectorization ***
***************************/

template<typename Kernel>
struct dense_assignment_loop<Kernel, SliceVectorizedTraversal, NoUnrolling>
{
  static inline void run(Kernel &kernel)
  {
    typedef typename Kernel::Index Index;
    typedef packet_traits<typename Kernel::Scalar> PacketTraits;
    enum {
      packetSize = PacketTraits::size,
      alignable = PacketTraits::AlignedOnScalar,
      dstAlignment = alignable ? Aligned : int(Kernel::AssignmentTraits::DstIsAligned)
    };
    const Index packetAlignedMask = packetSize - 1;
    const Index innerSize = kernel.innerSize();
    const Index outerSize = kernel.outerSize();
    const Index alignedStep = alignable ? (packetSize - kernel.outerStride() % packetSize) & packetAlignedMask : 0;
    Index alignedStart = ((!alignable) || Kernel::AssignmentTraits::DstIsAligned) ? 0
                       : internal::first_aligned(&kernel.dstEvaluator().coeffRef(0,0), innerSize);

    for(Index outer = 0; outer < outerSize; ++outer)
    {
      const Index alignedEnd = alignedStart + ((innerSize-alignedStart) & ~packetAlignedMask);
      // do the non-vectorizable part of the assignment
      for(Index inner = 0; inner<alignedStart ; ++inner)
        kernel.assignCoeffByOuterInner(outer, inner);

      // do the vectorizable part of the assignment
      for(Index inner = alignedStart; inner<alignedEnd; inner+=packetSize)
        kernel.template assignPacketByOuterInner<dstAlignment, Unaligned>(outer, inner);

      // do the non-vectorizable part of the assignment
      for(Index inner = alignedEnd; inner<innerSize ; ++inner)
        kernel.assignCoeffByOuterInner(outer, inner);

      alignedStart = std::min<Index>((alignedStart+alignedStep)%packetSize, innerSize);
    }
  }
};

/****************************
*** All-at-once traversal ***
****************************/

// TODO: this 'AllAtOnceTraversal' should be dropped or caught earlier (Gael)
// Indeed, what to do with the kernel's functor??
template<typename Kernel>
struct dense_assignment_loop<Kernel, AllAtOnceTraversal, NoUnrolling>
{
  static inline void run(Kernel & kernel)
  {
    // Evaluate rhs in temporary to prevent aliasing problems in a = a * a;
    // TODO: Do not pass the xpr object to evalTo() (Jitse)
    kernel.srcEvaluator().evalTo(kernel.dstEvaluator(), kernel.dstExpression());
  }
};

/***************************************************************************
* Part 4 : Generic dense assignment kernel
***************************************************************************/

// This class generalize the assignment of a coefficient (or packet) from one dense evaluator
// to another dense writable evaluator.
// It is parametrized by the two evaluators, and the actual assignment functor.
// This abstraction level permits to keep the evaluation loops as simple and as generic as possible.
// One can customize the assignment using this generic dense_assignment_kernel with different
// functors, or by completely overloading it, by-passing a functor.
template<typename DstEvaluatorTypeT, typename SrcEvaluatorTypeT, typename Functor>
class generic_dense_assignment_kernel
{
protected:
  typedef typename DstEvaluatorTypeT::XprType DstXprType;
  typedef typename SrcEvaluatorTypeT::XprType SrcXprType;
public:
  
  typedef DstEvaluatorTypeT DstEvaluatorType;
  typedef SrcEvaluatorTypeT SrcEvaluatorType;
  typedef typename DstEvaluatorType::Scalar Scalar;
  typedef typename DstEvaluatorType::Index Index;
  typedef copy_using_evaluator_traits<DstXprType, SrcXprType> AssignmentTraits;
  
  
  generic_dense_assignment_kernel(DstEvaluatorType &dst, const SrcEvaluatorType &src, const Functor &func, DstXprType& dstExpr)
    : m_dst(dst), m_src(src), m_functor(func), m_dstExpr(dstExpr)
  {}
  
  Index size() const        { return m_dstExpr.size(); }
  Index innerSize() const   { return m_dstExpr.innerSize(); }
  Index outerSize() const   { return m_dstExpr.outerSize(); }
  Index outerStride() const { return m_dstExpr.outerStride(); }
  
  // TODO get rid of this one:
  DstXprType& dstExpression() const { return m_dstExpr; }
  
  DstEvaluatorType& dstEvaluator() { return m_dst; }
  const SrcEvaluatorType& srcEvaluator() const { return m_src; }
  
  void assignCoeff(Index row, Index col)
  {
    m_functor.assignCoeff(m_dst.coeffRef(row,col), m_src.coeff(row,col));
  }
  
  void assignCoeff(Index index)
  {
    m_functor.assignCoeff(m_dst.coeffRef(index), m_src.coeff(index));
  }
  
  void assignCoeffByOuterInner(Index outer, Index inner)
  {
    Index row = rowIndexByOuterInner(outer, inner); 
    Index col = colIndexByOuterInner(outer, inner); 
    assignCoeff(row, col);
  }
  
  
  template<int StoreMode, int LoadMode>
  void assignPacket(Index row, Index col)
  {
    m_functor.assignPacket<StoreMode>(&m_dst.coeffRef(row,col), m_src.template packet<LoadMode>(row,col));
  }
  
  template<int StoreMode, int LoadMode>
  void assignPacket(Index index)
  {
    m_functor.assignPacket<StoreMode>(&m_dst.coeffRef(index), m_src.template packet<LoadMode>(index));
  }
  
  template<int StoreMode, int LoadMode>
  void assignPacketByOuterInner(Index outer, Index inner)
  {
    Index row = rowIndexByOuterInner(outer, inner); 
    Index col = colIndexByOuterInner(outer, inner);
    assignPacket<StoreMode,LoadMode>(row, col);
  }
  
  static Index rowIndexByOuterInner(Index outer, Index inner)
  {
    typedef typename DstEvaluatorType::ExpressionTraits Traits;
    return int(Traits::RowsAtCompileTime) == 1 ? 0
      : int(Traits::ColsAtCompileTime) == 1 ? inner
      : int(Traits::Flags)&RowMajorBit ? outer
      : inner;
  }

  static Index colIndexByOuterInner(Index outer, Index inner)
  {
    typedef typename DstEvaluatorType::ExpressionTraits Traits;
    return int(Traits::ColsAtCompileTime) == 1 ? 0
      : int(Traits::RowsAtCompileTime) == 1 ? inner
      : int(Traits::Flags)&RowMajorBit ? inner
      : outer;
  }
  
protected:
  DstEvaluatorType& m_dst;
  const SrcEvaluatorType& m_src;
  const Functor &m_functor;
  // TODO find a way to avoid the needs of the original expression
  DstXprType& m_dstExpr;
};

/***************************************************************************
* Part 5 : Entry point for dense rectangular assignment
***************************************************************************/

template<typename DstXprType, typename SrcXprType, typename Functor>
void call_dense_assignment_loop(const DstXprType& dst, const SrcXprType& src, const Functor &func)
{
#ifdef EIGEN_DEBUG_ASSIGN
  // TODO these traits should be computed from information provided by the evaluators
  internal::copy_using_evaluator_traits<DstXprType, SrcXprType>::debug();
#endif
  eigen_assert(dst.rows() == src.rows() && dst.cols() == src.cols());
  
  typedef typename evaluator<DstXprType>::type DstEvaluatorType;
  typedef typename evaluator<SrcXprType>::type SrcEvaluatorType;

  DstEvaluatorType dstEvaluator(dst);
  SrcEvaluatorType srcEvaluator(src);
    
  typedef generic_dense_assignment_kernel<DstEvaluatorType,SrcEvaluatorType,Functor> Kernel;
  Kernel kernel(dstEvaluator, srcEvaluator, func, dst.const_cast_derived());
  
  dense_assignment_loop<Kernel>::run(kernel);
}

template<typename DstXprType, typename SrcXprType>
void call_dense_assignment_loop(const DstXprType& dst, const SrcXprType& src)
{
  call_dense_assignment_loop(dst, src, internal::assign_op<typename DstXprType::Scalar>());
}

/***************************************************************************
* Part 6 : Generic assignment
***************************************************************************/


// An evaluator must define its shape. It can be one of the following:
struct DenseShape       {};
struct DiagonalShape    {};
struct BandShape        {};
struct TriangularShape  {};
struct SelfAdjointShape {};
struct SparseShape      {};


// Based on the respective shapes of the destination and source,
// the class AssignmentKind determine the kind of assignment mechanism.
// AssignmentKind must define a Kind typedef.
template<int DstShape, int SrcShape> struct AssignmentKind;

// AssignmentKind<.,.>::Kind can be one of the following:
    struct Dense2Dense                  {};
    struct Triangular2Triangular        {};
//  struct Diagonal2Diagonal            {}; // <=> Dense2Dense
    struct Sparse2Dense                 {};
    struct Sparse2Sparse                {};

// This is the main assignment class
template< typename DstXprType, typename SrcXprType, typename Functor,
          typename Kind = Dense2Dense,//AssignmentKind< evaluator<A>::Shape , evaluator<B>::Shape >::Kind,
          typename Scalar = typename DstXprType::Scalar>
struct Assignment;


// The only purpose of this call_assignment() function is to deal with noalias() / AssumeAliasing.
// Indeed, I (Gael) think that this concept of AssumeAliasing was a mistake, and it makes thing quite complicated.
// So this intermediate function removes everything related to AssumeAliasing such that Assignment
// does not has to bother about these annoying details.

template<typename Dst, typename Src, typename Func>
void call_assignment(Dst& dst, const Src& src, const Func& func)
{
  typedef typename internal::conditional<evaluator_traits<Src>::AssumeAliasing==1, EvalToTemp<Src>, Src>::type ActualSrc;
  Assignment<Dst,ActualSrc,Func>::run(dst, src, func);
}

// by-pass AssumeAliasing
template<typename Dst, template <typename> class StorageBase, typename Src, typename Func>
void call_assignment(const NoAlias<Dst,StorageBase>& dst, const Src& src, const Func& func)
{
  Assignment<Dst,Src,Func>::run(dst.expression(), src, func);
}

// Generic Dense to Dense assignment
template< typename DstXprType, typename SrcXprType, typename Functor, typename Scalar>
struct Assignment<DstXprType, SrcXprType, Functor, Dense2Dense, Scalar>
{
  static void run(DstXprType &dst, const SrcXprType &src, const Functor &func)
  {
    // TODO check whether this is the right place to perform these checks:
    enum{
      SameType = internal::is_same<typename DstXprType::Scalar,typename SrcXprType::Scalar>::value
    };

    EIGEN_STATIC_ASSERT_LVALUE(DstXprType)
    EIGEN_STATIC_ASSERT_SAME_MATRIX_SIZE(DstXprType,SrcXprType)
    EIGEN_STATIC_ASSERT(SameType,YOU_MIXED_DIFFERENT_NUMERIC_TYPES__YOU_NEED_TO_USE_THE_CAST_METHOD_OF_MATRIXBASE_TO_CAST_NUMERIC_TYPES_EXPLICITLY)
    
    eigen_assert(dst.rows() == src.rows() && dst.cols() == src.cols());
    
    #ifdef EIGEN_DEBUG_ASSIGN
      internal::copy_using_evaluator_traits<DstXprType, SrcXprType>::debug();
    #endif
    
    call_dense_assignment_loop(dst, src, func);
  }
};

} // namespace internal

} // end namespace Eigen

#endif // EIGEN_ASSIGN_EVALUATOR_H
