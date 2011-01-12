/*=========================================================================

Program:   Insight Segmentation & Registration Toolkit
Module:    $RCSfile: itkKernelTransform2.txx,v $
Language:  C++
Date:      $Date: 2006-11-28 14:22:18 $
Version:   $Revision: 1.1 $

Copyright (c) Insight Software Consortium. All rights reserved.
See ITKCopyright.txt or http://www.itk.org/HTML/Copyright.htm for details.

This software is distributed WITHOUT ANY WARRANTY; without even
the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE.  See the above copyright notices for more information.

=========================================================================*/
#ifndef _itkKernelTransform2_txx
#define _itkKernelTransform2_txx

#include "itkKernelTransform2.h"


namespace itk
{

 /**
  *
  */
template <class TScalarType, unsigned int NDimensions>
KernelTransform2<TScalarType, NDimensions>
::KernelTransform2() : Superclass( NDimensions, 0 )
    // the 0 is provided as
    // a tentative number for initializing the Jacobian.
    // The matrix can be resized at run time so this number
    // here is irrelevant. The correct size of the Jacobian
    // will be NDimension X NDimension.NumberOfLandMarks.
{
  this->m_I.set_identity();
  this->m_SourceLandmarks = PointSetType::New();
  this->m_TargetLandmarks = PointSetType::New();
  this->m_Displacements   = VectorSetType::New();
  this->m_WMatrixComputed = false;
  

  this->m_LMatrixComputed = false;
  this->m_LInverseComputed = false;
  this->m_LMatrixDecompositionComputed = false;

  this->m_LMatrixDecompositionSVD = 0;
  this->m_LMatrixDecompositionQR = 0;

  this->m_Stiffness = 0.0;

  // dummy value:
  this->m_PoissonRatio = 0.3;

  this->m_MatrixInversionMethod = "SVD";
  this->m_FastComputationPossible = false;

} // end constructor


template <class TScalarType, unsigned int NDimensions>
KernelTransform2<TScalarType, NDimensions>
::~KernelTransform2()
{
  delete m_LMatrixDecompositionSVD;
  delete m_LMatrixDecompositionQR;

} // end destructor

  /**
  *
  */
template <class TScalarType, unsigned int NDimensions>
void
KernelTransform2<TScalarType, NDimensions>
::SetSourceLandmarks( PointSetType * landmarks )
{
  itkDebugMacro( << "setting SourceLandmarks to " << landmarks );
  if ( this->m_SourceLandmarks != landmarks )
  {
    this->m_SourceLandmarks = landmarks;
    //this->UpdateParameters(); // Not affected by source landmark change
    this->Modified();

    // these are invalidated when the source landmarks change
    this->m_WMatrixComputed = false;
    this->m_LMatrixComputed = false;
    this->m_LInverseComputed = false;
    this->m_LMatrixDecompositionComputed = false;

    // you must recompute L and Linv - this does not require the targ landmarks
    this->ComputeLInverse();

    // precompute the nonzerojacobianindices vector
    unsigned long nrParams = this->GetNumberOfParameters();
    this->m_NonZeroJacobianIndices.resize( nrParams );
    for ( unsigned int i = 0; i < nrParams; ++i )
    {
      this->m_NonZeroJacobianIndices[ i ] = i;
    }
  }

} // end SetSourceLandmarks()


  /**
  *
  */
template <class TScalarType, unsigned int NDimensions>
void
KernelTransform2<TScalarType, NDimensions>
::SetTargetLandmarks( PointSetType * landmarks )
{
  itkDebugMacro( "setting TargetLandmarks to " << landmarks );
  if ( this->m_TargetLandmarks != landmarks )
  {
    this->m_TargetLandmarks = landmarks;

    // this is invalidated when the target landmarks change
    this->m_WMatrixComputed = false;
    this->ComputeWMatrix();
    this->UpdateParameters();
    this->Modified();
  }

} // end SetTargetLandmarks()


/**
 * **************** ComputeG ***********************************
 */

template <class TScalarType, unsigned int NDimensions>
void
KernelTransform2<TScalarType, NDimensions>
::ComputeG( const InputVectorType &, GMatrixType & ) const
{
  itkExceptionMacro( << "ComputeG() should be reimplemented in the subclass !!" );
} // end ComputeG()


/**
 *
 */

template <class TScalarType, unsigned int NDimensions>
void
KernelTransform2<TScalarType, NDimensions>
::ComputeReflexiveG( PointsIterator, GMatrixType & GMatrix ) const
{
  GMatrix.fill( NumericTraits< TScalarType >::Zero );
  GMatrix.fill_diagonal( this->m_Stiffness );

} // end ComputeReflexiveG()


/**
 * Default implementation of the the method. This can be overloaded
 * in transforms whose kernel produce diagonal G matrices.
 */

template <class TScalarType, unsigned int NDimensions>
void
KernelTransform2<TScalarType, NDimensions>
::ComputeDeformationContribution(
  const InputPointType & thisPoint, OutputPointType & opp ) const
{
  const unsigned long numberOfLandmarks = this->m_SourceLandmarks->GetNumberOfPoints();

  PointsIterator sp = this->m_SourceLandmarks->GetPoints()->Begin();
  GMatrixType Gmatrix;

  for ( unsigned long lnd = 0; lnd < numberOfLandmarks; lnd++ )
  {
    this->ComputeG( thisPoint - sp->Value(), Gmatrix );
    for ( unsigned int dim = 0; dim < NDimensions; dim++ )
    {
      for ( unsigned int odim = 0; odim < NDimensions; odim++ )
      {
        opp[ odim ] += Gmatrix( dim, odim ) * this->m_DMatrix( dim, lnd );
      }
    }
    ++sp;
  }

} // end ComputeDeformationContribution()


/**
 *
 */

template <class TScalarType, unsigned int NDimensions>
void
KernelTransform2<TScalarType, NDimensions>
::ComputeD( void )
{
  const unsigned long numberOfLandmarks = this->m_SourceLandmarks->GetNumberOfPoints();

  PointsIterator sp  = this->m_SourceLandmarks->GetPoints()->Begin();
  PointsIterator tp  = this->m_TargetLandmarks->GetPoints()->Begin();
  PointsIterator end = this->m_SourceLandmarks->GetPoints()->End();

  this->m_Displacements->Reserve( numberOfLandmarks );
  typename VectorSetType::Iterator vt = this->m_Displacements->Begin();
  while ( sp != end )
  {
    vt->Value() = tp->Value() - sp->Value();
    vt++;
    sp++;
    tp++;
  }

} // end ComputeD()


/**
 *
 */

template <class TScalarType, unsigned int NDimensions>
void
KernelTransform2<TScalarType, NDimensions>
::ComputeWMatrix( void )
{
  /** Compute L and Y. */
  if ( !this->m_LMatrixComputed )
  {
    this->ComputeL();
  }
  this->ComputeY();

  /** L matrix decomposition and solving for Y matrix. */
  if ( this->m_MatrixInversionMethod == "SVD" )
  {
    if ( !this->m_LMatrixDecompositionComputed )
    {
      if ( this->m_LMatrixDecompositionSVD != 0 )
      {
        delete this->m_LMatrixDecompositionSVD;
      }
      this->m_LMatrixDecompositionSVD = new SVDDecompositionType( this->m_LMatrix, 1e-8 );
      this->m_LMatrixDecompositionComputed = true;
    }
    this->m_WMatrix = this->m_LMatrixDecompositionSVD->solve( this->m_YMatrix );
    //vnl_svd<TScalarType> svd( this->m_LMatrix, 1e-8 );
    //this->m_WMatrix = svd.solve( this->m_YMatrix );
  }
  else if ( this->m_MatrixInversionMethod == "QR" )
  {
    if ( !this->m_LMatrixDecompositionComputed )
    {
      if ( this->m_LMatrixDecompositionQR != 0 )
      {
        delete this->m_LMatrixDecompositionQR;
      }
      this->m_LMatrixDecompositionQR = new QRDecompositionType( this->m_LMatrix );
      this->m_LMatrixDecompositionComputed = true;
    }
    this->m_WMatrix = this->m_LMatrixDecompositionQR->solve( this->m_YMatrix );
//     vnl_qr<TScalarType> qr( this->m_LMatrix );
//     this->m_WMatrix = qr.solve( this->m_YMatrix );
  }
  else
  {
    itkExceptionMacro( << "ERROR: invalid matrix inversion method ("
      << this->m_MatrixInversionMethod << ")" );
  }

  /** Reorganize W. */
  this->ReorganizeW();
  this->m_WMatrixComputed = true;

} // end ComputeWMatrix()


/**
 *
 */

template <class TScalarType, unsigned int NDimensions>
void
KernelTransform2<TScalarType, NDimensions>
::ComputeLInverse( void )
{
  if ( !this->m_LMatrixComputed )
  {
    this->ComputeL();
  }

  if ( this->m_MatrixInversionMethod == "SVD" )
  {
    //this->m_LMatrixInverse = vnl_matrix_inverse<TScalarType>( this->m_LMatrix );
    this->m_LMatrixInverse = vnl_svd<TScalarType>( this->m_LMatrix ).inverse();
    this->m_LInverseComputed = true;
  }
  else if ( this->m_MatrixInversionMethod == "QR" )
  {
    this->m_LMatrixInverse = vnl_qr<TScalarType>( this->m_LMatrix ).inverse();
    this->m_LInverseComputed = true;
  }
  else
  {
    itkExceptionMacro( << "ERROR: invalid matrix inversion method ("
      << this->m_MatrixInversionMethod << ")" );
    this->m_LInverseComputed = false;
  }

} // end ComputeLInverse()


/**
 *
 */

template <class TScalarType, unsigned int NDimensions>
void
KernelTransform2<TScalarType, NDimensions>
::ComputeL( void )
{
  const unsigned long numberOfLandmarks = this->m_SourceLandmarks->GetNumberOfPoints();
  vnl_matrix<TScalarType> O2( NDimensions * ( NDimensions + 1 ),
    NDimensions * ( NDimensions + 1 ), 0 );

  this->ComputeP();
  this->ComputeK();

  this->m_LMatrix.set_size( NDimensions * ( numberOfLandmarks + NDimensions + 1 ),
    NDimensions * ( numberOfLandmarks + NDimensions + 1 ) );
  this->m_LMatrix.fill( 0.0 );
  this->m_LMatrix.update( this->m_KMatrix, 0, 0 );
  this->m_LMatrix.update( this->m_PMatrix, 0, this->m_KMatrix.columns() );
  this->m_LMatrix.update( this->m_PMatrix.transpose(), this->m_KMatrix.rows(), 0 );
  this->m_LMatrix.update( O2, this->m_KMatrix.rows(), this->m_KMatrix.columns() );
  this->m_LMatrixComputed = true;
  this->m_LMatrixDecompositionComputed = false;

} // end ComputeL()


/**
 *
 */

template <class TScalarType, unsigned int NDimensions>
void
KernelTransform2<TScalarType, NDimensions>
::ComputeK( void )
{
  const unsigned long numberOfLandmarks = this->m_SourceLandmarks->GetNumberOfPoints();
  GMatrixType G;

  this->m_KMatrix.set_size( NDimensions * numberOfLandmarks,
    NDimensions * numberOfLandmarks );
  this->m_KMatrix.fill( 0.0 );

  PointsIterator p1  = this->m_SourceLandmarks->GetPoints()->Begin();
  PointsIterator end = this->m_SourceLandmarks->GetPoints()->End();

  // K matrix is symmetric, so only evaluate the upper triangle and
  // store the values in bot the upper and lower triangle
  unsigned int i = 0;
  while ( p1 != end )
  {
    PointsIterator p2 = p1; // start at the diagonal element
    unsigned int j = i;

    // Compute the block diagonal element, i.e. kernel for pi->pi
    this->ComputeReflexiveG( p1, G );
    this->m_KMatrix.update( G, i * NDimensions, i * NDimensions );
    p2++; j++;

    // Compute the upper (and copy into lower) triangular part of K
    while ( p2 != end )
    {
      const InputVectorType s = p1.Value() - p2.Value();
      this->ComputeG( s, G );
      // write value in upper and lower triangle of matrix
//       if ( !this->m_FastComputationPossible )
//       {
        this->m_KMatrix.update( G, i * NDimensions, j * NDimensions );
        this->m_KMatrix.update( G, j * NDimensions, i * NDimensions );
// Possible, but no speed gain
//       }
//       else
//       {
//         ScalarType g = G( 0,0 );
//         for ( unsigned int idx = 0; idx < NDimensions; idx++ )
//         {
//           this->m_KMatrix( i * NDimensions + idx, j * NDimensions + idx ) = g;
//           this->m_KMatrix( j * NDimensions + idx, i * NDimensions + idx ) = g;
//         }
//       }
      p2++; j++;
    }
    p1++; i++;
  }

} // end ComputeK()


/**
 *
 */

template <class TScalarType, unsigned int NDimensions>
void
KernelTransform2<TScalarType, NDimensions>
::ComputeP( void )
{
  const unsigned long numberOfLandmarks = this->m_SourceLandmarks->GetNumberOfPoints();
  IMatrixType I; I.set_identity();
  IMatrixType temp;
  InputPointType p; p.Fill( 0.0f );

  this->m_PMatrix.set_size( NDimensions * numberOfLandmarks,
    NDimensions * ( NDimensions + 1 ) );
  this->m_PMatrix.fill( 0.0f );

  for ( unsigned long i = 0; i < numberOfLandmarks; i++ )
  {
    this->m_SourceLandmarks->GetPoint( i, &p );
    for ( unsigned int j = 0; j < NDimensions; j++)
    {
      temp = I * p[ j ];
      this->m_PMatrix.update( temp, i * NDimensions, j * NDimensions );
    }
    this->m_PMatrix.update( I, i * NDimensions, NDimensions * NDimensions );
  }

} // end ComputeP()


/**
 *
 */

template <class TScalarType, unsigned int NDimensions>
void
KernelTransform2<TScalarType, NDimensions>
::ComputeY( void )
{
  const unsigned long numberOfLandmarks = this->m_SourceLandmarks->GetNumberOfPoints();
  this->ComputeD();

  typename VectorSetType::ConstIterator displacement = this->m_Displacements->Begin();

  this->m_YMatrix.set_size( NDimensions * ( numberOfLandmarks + NDimensions + 1 ), 1 );
  this->m_YMatrix.fill( 0.0 );

  for ( unsigned long i = 0; i < numberOfLandmarks; i++ )
  {
    for ( unsigned int j = 0; j < NDimensions; j++ )
    {
      this->m_YMatrix.put( i * NDimensions + j, 0, displacement.Value()[ j ] );
    }
    displacement++;
  }

  for ( unsigned int i = 0; i < NDimensions * ( NDimensions + 1 ); i++ )
  {
    this->m_YMatrix.put( numberOfLandmarks * NDimensions + i, 0, 0 );
  }

} // end ComputeY()


/**
*
*/

template <class TScalarType, unsigned int NDimensions>
void
KernelTransform2<TScalarType, NDimensions>
::ReorganizeW( void )
{
  const unsigned long numberOfLandmarks = this->m_SourceLandmarks->GetNumberOfPoints();

  // The deformable (non-affine) part of the registration goes here
  this->m_DMatrix.set_size( NDimensions, numberOfLandmarks );
  unsigned int ci = 0;

  for ( unsigned long lnd = 0; lnd < numberOfLandmarks; lnd++ )
  {
    for ( unsigned int dim = 0; dim < NDimensions; dim++ )
    {
      this->m_DMatrix( dim, lnd ) = this->m_WMatrix( ci++, 0 );
    }
  }

  // This matrix holds the rotational part of the Affine component
  for ( unsigned int j = 0; j < NDimensions; j++ )
  {
    for ( unsigned int i = 0; i < NDimensions; i++ )
    {
      this->m_AMatrix( i, j ) = this->m_WMatrix( ci++, 0 );
    }
  }

  // This vector holds the translational part of the Affine component
  for ( unsigned int k = 0; k < NDimensions; k++ )
  {
    this->m_BVector( k ) = this->m_WMatrix( ci++, 0 );
  }

  // release WMatrix memory by assigning a small one.
  this->m_WMatrix = WMatrixType( 1, 1 );
  this->m_WMatrixComputed = true;

} // end ReorganizeW()


/**
 *
 */

template <class TScalarType, unsigned int NDimensions>
typename KernelTransform2<TScalarType, NDimensions>::OutputPointType
KernelTransform2<TScalarType, NDimensions>
::TransformPoint( const InputPointType & thisPoint ) const
{
  OutputPointType opp;
  opp.Fill( NumericTraits<typename OutputPointType::ValueType>::Zero );
  this->ComputeDeformationContribution( thisPoint, opp );

  // Add the rotational part of the Affine component
  for ( unsigned int j = 0; j < NDimensions; j++ )
  {
    for ( unsigned int i = 0; i < NDimensions; i++ )
    {
      opp[ i ] += this->m_AMatrix( i, j ) * thisPoint[ j ];
    }
  }

  // This vector holds the translational part of the Affine component
  for ( unsigned int k = 0; k < NDimensions; k++ )
  {
    opp[ k ] += this->m_BVector( k ) + thisPoint[ k ];
  }

  return opp;

} // end TransformPoint()


// Compute the Jacobian in one position

template <class TScalarType, unsigned int NDimensions>
const typename KernelTransform2<TScalarType,NDimensions>::JacobianType &
KernelTransform2< TScalarType,NDimensions>
::GetJacobian( const InputPointType & thisPoint ) const
{
  this->GetJacobian( thisPoint, this->m_Jacobian, this->m_NonZeroJacobianIndicesTemp );
  return this->m_Jacobian;

} // end GetJacobian()

// Set to the identity transform - ie make the  Source and target lm the same

template <class TScalarType, unsigned int NDimensions>
void
KernelTransform2<TScalarType, NDimensions>
::SetIdentity( void )
{
  this->SetParameters( this->GetFixedParameters() );
} // end SetIdentity()


  // Set the parameters
  // NOTE that in this transformation both the Source and Target
  // landmarks could be considered as parameters. It is assumed
  // here that the Target landmarks are provided by the user and
  // are not changed during the optimization process required for
  // registration.

template <class TScalarType, unsigned int NDimensions>
void
KernelTransform2<TScalarType, NDimensions>
::SetParameters( const ParametersType & parameters )
{
  this->m_Parameters = parameters;
  typename PointsContainer::Pointer landmarks = PointsContainer::New();
  const unsigned int numberOfLandmarks = parameters.Size() / NDimensions;
  landmarks->Reserve( numberOfLandmarks );

  PointsIterator itr = landmarks->Begin();
  PointsIterator end = landmarks->End();
  InputPointType  landMark;
  unsigned int pcounter = 0;

  while ( itr != end )
  {
    for ( unsigned int dim = 0; dim < NDimensions; dim++ )
    {
      landMark[ dim ] = parameters[ pcounter ];
      pcounter++;
    }
    itr.Value() = landMark;
    itr++;
  }

  this->m_TargetLandmarks->SetPoints( landmarks );

  // W MUST be recomputed if the target lms are set
  this->ComputeWMatrix();

  // Modified is always called since we just have a pointer to the
  // parameters and cannot know if the parameters have changed.
  this->Modified();

} // end SetParameters()


  // Set the fixed parameters
  // Since the API of the SetParameters() function sets the
  // source landmarks, this function was added to support the
  // setting of the target landmarks, and allowing the Transform
  // I/O mechanism to be supported.

template <class TScalarType, unsigned int NDimensions>
void
KernelTransform2<TScalarType, NDimensions>
::SetFixedParameters( const ParametersType & parameters )
{
  typename PointsContainer::Pointer landmarks = PointsContainer::New();
  const unsigned int numberOfLandmarks = parameters.Size() / NDimensions;
  landmarks->Reserve( numberOfLandmarks );

  PointsIterator itr = landmarks->Begin();
  PointsIterator end = landmarks->End();
  InputPointType  landMark;
  unsigned int pcounter = 0;

  while ( itr != end )
  {
    for ( unsigned int dim = 0; dim < NDimensions; dim++ )
    {
      landMark[ dim ] = parameters[ pcounter ];
      pcounter++;
    }
    itr.Value() = landMark;
    itr++;
  }

  this->m_SourceLandmarks->SetPoints( landmarks );

  // these are invalidated when the source lms change
  this->m_WMatrixComputed = false;
  this->m_LMatrixComputed = false;
  this->m_LInverseComputed = false;
  this->m_LMatrixDecompositionComputed = false;

  // you must recompute L and Linv - this does not require the targ lms
  this->ComputeLInverse();

} // end SetFixedParameters()


// Update parameters array
// They are the components of all the landmarks in the source space

template <class TScalarType, unsigned int NDimensions>
void
KernelTransform2<TScalarType, NDimensions>
::UpdateParameters( void )
{
  this->m_Parameters = ParametersType( this->m_TargetLandmarks->GetNumberOfPoints() * NDimensions );

  PointsIterator itr = this->m_TargetLandmarks->GetPoints()->Begin();
  PointsIterator end = this->m_TargetLandmarks->GetPoints()->End();
  unsigned int pcounter = 0;

  while ( itr != end )
  {
    InputPointType landmark = itr.Value();
    for ( unsigned int dim = 0; dim < NDimensions; dim++ )
    {
      this->m_Parameters[ pcounter ] = landmark[ dim ];
      pcounter++;
    }
    itr++;
  }

} // end UpdateParameters()


// Get the parameters
// They are the components of all the landmarks in the source space

template <class TScalarType, unsigned int NDimensions>
const typename KernelTransform2<TScalarType, NDimensions>::ParametersType &
KernelTransform2<TScalarType, NDimensions>
::GetParameters( void ) const
{
  return this->m_Parameters;
} // end GetParameters()


  // Get the fixed parameters
  // This returns the target landmark locations
  // This was added to support the Transform Reader/Writer mechanism
template <class TScalarType, unsigned int NDimensions>
const typename KernelTransform2<TScalarType, NDimensions>::ParametersType &
KernelTransform2<TScalarType, NDimensions>
::GetFixedParameters( void ) const
{
  this->m_FixedParameters = ParametersType(
    this->m_SourceLandmarks->GetNumberOfPoints() * NDimensions );
  PointsIterator itr = this->m_SourceLandmarks->GetPoints()->Begin();
  PointsIterator end = this->m_SourceLandmarks->GetPoints()->End();
  unsigned int pcounter = 0;

  while ( itr != end )
  {
    InputPointType landmark = itr.Value();
    for ( unsigned int dim = 0; dim < NDimensions; dim++ )
    {
      this->m_FixedParameters[ pcounter ] = landmark[ dim ];
      pcounter++;
    }
    itr++;
  }

  return this->m_FixedParameters;

} // end GetFixedParameters()


 /**
  * ********************* GetJacobian ****************************
  */

template <class TScalarType, unsigned int NDimensions>
void
KernelTransform2<TScalarType, NDimensions>
::GetJacobian( const InputPointType & p, JacobianType & jac,
  NonZeroJacobianIndicesType & nonZeroJacobianIndices ) const
{
  const unsigned long numberOfLandmarks = this->m_SourceLandmarks->GetNumberOfPoints();
  jac.SetSize( NDimensions, numberOfLandmarks * NDimensions );
  jac.Fill( 0.0 );
  GMatrixType Gmatrix, GMatrixSym; // dim x dim
  PointsIterator sp = this->m_SourceLandmarks->GetPoints()->Begin();

  // General route working for all kernels (but slow)
  if ( !this->m_FastComputationPossible )
  {
    for ( unsigned int lnd = 0; lnd < numberOfLandmarks; lnd++ )
    {
      this->ComputeG( p - sp->Value(), Gmatrix );
      for ( unsigned int dim = 0; dim < NDimensions; dim++ )
      {
        for ( unsigned int odim = 0; odim < NDimensions; odim++ )
        {
          for ( unsigned int lidx = 0; lidx < numberOfLandmarks * NDimensions; lidx++ )
          {
            jac[ odim ][ lidx ] += Gmatrix( dim, odim )
              * this->m_LMatrixInverse[ lnd * NDimensions + dim ][ lidx ];
          }
        }
      }
      ++sp;
    }

    for ( unsigned int odim = 0; odim < NDimensions; odim++ )
    {
      for ( unsigned long lidx = 0; lidx < numberOfLandmarks * NDimensions; lidx++ )
      {
        for ( unsigned int dim = 0; dim < NDimensions; dim++ )
        {
          jac[ odim ][lidx] += p[ dim ]
            * this->m_LMatrixInverse[ ( numberOfLandmarks + dim ) * NDimensions + odim ][ lidx ];
        }
        const unsigned long index = ( numberOfLandmarks + NDimensions ) * NDimensions + odim;
        jac[ odim ][lidx] += this->m_LMatrixInverse[ index ][ lidx ];
      }
    }
  } // if !this->m_FastComputationPossible
  // Define n = the number of landmarks, d = dimension, Linv = inverse L matrix.
  // For some of the kernel transform it is possible to use optimized paths:
  //   - ThinPlateR2LogRSplineKernelTransform2
  //   - ThinPlateSplineKernelTransform2
  //   - VolumeSplineKernelTransform2
  // These kernel transforms have the following properties, that can be exploited
  // to increase Jacobian computation performance:
  // A1) G is a d x d diagonal matrix, meaning it is sparse
  // A2) G is diagonal, with identical values on the main diagonal,
  //     i.e. G = G(0,0) * I_d, so it is fully defined by just 1 value G(0,0).
  // A1 and A2 together reduce the memory access to G from d x d to 1.
  //
  // B1) Linv is an ( n x d + y )^2 block diagonal matrix:
  //     it is very sparse, with 2/4 = 50%, resp. 3/9 = 33% non-zero values.
  // B2) Linv is block diagonal, with identical values on the main diagonal
  //     of each block, i.e. each block is fully defined by just 1 value.
  // B1 and B2 together reduce the memory access to Linv also with a factor d x d.
  //
  // C) For all kernels, both Linv and G are symmetric.
  //    Reduces memory access to Linv by a factor 2.
  else
  {
    // Precompute G's.
    std::vector<ScalarType> gVector( numberOfLandmarks );
    for ( unsigned int lnd = 0; lnd < numberOfLandmarks; lnd++ )
    {
      // Property A: G = G(0,0) * I_d.
      this->ComputeG( p - sp->Value(), Gmatrix );
      gVector[ lnd ] = Gmatrix( 0, 0 );
      ++sp;
    }

    // Deformation part of the transform:
    sp = this->m_SourceLandmarks->GetPoints()->Begin();
    for ( unsigned int lnd = 0; lnd < numberOfLandmarks; lnd++ )
    {
      // Property A: G = G(0,0) * I_d.
      ScalarType g = gVector[ lnd ];

      // Property C: First process the diagonal only
      unsigned int lIdx = lnd * NDimensions;
      ScalarType linv = this->m_LMatrixInverse[ lIdx ][ lIdx ];
      // Property B: only access non-zero values
      for ( unsigned int dim = 0; dim < NDimensions; dim++ )
      {
        jac[ dim ][ lIdx + dim ] += g * linv;
      }

      // Property C: Then process right of diagonal
      for ( unsigned int lidx = lnd + 1; lidx < numberOfLandmarks; lidx++ )
      {
        // Property C: Get G value at mirrored position
        ScalarType gSym = gVector[ lidx ];

        // Property B: only access non-zero values
        unsigned int lIdx = lidx * NDimensions;
        ScalarType linv = this->m_LMatrixInverse[ lnd * NDimensions ][ lIdx ];

        // Property B: only access non-zero values
        for ( unsigned int dim = 0; dim < NDimensions; dim++ )
        {
          jac[ dim ][ lIdx + dim ] += g * linv;
          // Property C: mirroring
          jac[ dim ][ lnd * NDimensions + dim ] += gSym * linv;
        }
      } // end for lidx

      // Next source landmark
      ++sp;
    }

    // Affine part of the transform:
    for ( unsigned int odim = 0; odim < NDimensions; odim++ )
    {
      const unsigned long index = ( numberOfLandmarks + NDimensions ) * NDimensions + odim;

      for ( unsigned long lidx = 0; lidx < numberOfLandmarks * NDimensions; lidx++ )
      {
        ScalarType tmp = 0.0;
        for ( unsigned int dim = 0; dim < NDimensions; dim++ )
        {
          unsigned int indtmp = ( numberOfLandmarks + dim ) * NDimensions + odim;
          tmp += p[ dim ] * this->m_LMatrixInverse[ indtmp ][ lidx ];
        }
        jac[ odim ][ lidx ] += tmp + this->m_LMatrixInverse[ index ][ lidx ];
      }
    }
  } // end if this->m_FastComputationPossible

  nonZeroJacobianIndices = this->m_NonZeroJacobianIndices;

} // end GetJacobian()


template <class TScalarType, unsigned int NDimensions>
void
KernelTransform2<TScalarType, NDimensions>
::PrintSelf(std::ostream& os, Indent indent) const
{
  Superclass::PrintSelf( os, indent );

  if ( this->m_SourceLandmarks )
  {
    os << indent << "SourceLandmarks: " << std::endl;
    this->m_SourceLandmarks->Print( os, indent.GetNextIndent() );
  }
  if ( this->m_TargetLandmarks )
  {
    os << indent << "TargetLandmarks: " << std::endl;
    this->m_TargetLandmarks->Print( os, indent.GetNextIndent() );
  }
  if ( this->m_Displacements )
  {
    os << indent << "Displacements: " << std::endl;
    this->m_Displacements->Print( os, indent.GetNextIndent() );
  }
  os << indent << "Stiffness: " << m_Stiffness << std::endl;

} // end PrintSelf()


} // namespace itk


#endif
