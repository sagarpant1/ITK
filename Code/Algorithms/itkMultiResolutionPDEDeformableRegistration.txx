/*=========================================================================

  Program:   Insight Segmentation & Registration Toolkit
  Module:    itkMultiResolutionPDEDeformableRegistration.txx
  Language:  C++
  Date:      $Date$
  Version:   $Revision$

  Copyright (c) 2002 Insight Consortium. All rights reserved.
  See ITKCopyright.txt or http://www.itk.org/HTML/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even 
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
     PURPOSE.  See the above copyright notices for more information.

=========================================================================*/
#ifndef _itkMultiResolutionPDEDeformableRegistration_txx
#define _itkMultiResolutionPDEDeformableRegistration_txx
#include "itkMultiResolutionPDEDeformableRegistration.h"

#include "itkImageRegionIterator.h"
#include "vnl/vnl_math.h"

namespace itk {

/*
 * Default constructor
 */
template <class TReference, class TTarget, class TDeformationField>
MultiResolutionPDEDeformableRegistration<TReference,TTarget,TDeformationField>
::MultiResolutionPDEDeformableRegistration()
{
 
  this->SetNumberOfRequiredInputs(3);

  typename DefaultRegistrationType::Pointer registrator =
    DefaultRegistrationType::New();
  m_RegistrationFilter = static_cast<RegistrationType*>(
    registrator.GetPointer() );

  m_ReferencePyramid  = ReferencePyramidType::New();
  m_TargetPyramid     = TargetPyramidType::New();
  m_FieldExpander     = FieldExpanderType::New();

  m_NumberOfLevels = 3;
  m_NumberOfIterations.resize( m_NumberOfLevels );
  m_ReferencePyramid->SetNumberOfLevels( m_NumberOfLevels );
  m_TargetPyramid->SetNumberOfLevels( m_NumberOfLevels );

  unsigned int ilevel;
  for( ilevel = 0; ilevel < m_NumberOfLevels; ilevel++ )
    {
    m_NumberOfIterations[ilevel] = 10;
    }
  m_CurrentLevel = 0;

}


/*
 * Set the reference image.
 */
template <class TReference, class TTarget, class TDeformationField>
void
MultiResolutionPDEDeformableRegistration<TReference,TTarget,TDeformationField>
::SetReference(
const ReferenceType * ptr )
{
  this->ProcessObject::SetNthInput( 1, const_cast< ReferenceType * >( ptr ) );
}


/*
 * Get the reference image.
 */
template <class TReference, class TTarget, class TDeformationField>
MultiResolutionPDEDeformableRegistration<TReference,TTarget,TDeformationField>
::ReferenceConstPointer
MultiResolutionPDEDeformableRegistration<TReference,TTarget,TDeformationField>
::GetReference(void)
{
  return dynamic_cast< const ReferenceType * >
    ( this->ProcessObject::GetInput( 1 ).GetPointer() );
}


/*
 * Set the target image.
 */
template <class TReference, class TTarget, class TDeformationField>
void
MultiResolutionPDEDeformableRegistration<TReference,TTarget,TDeformationField>
::SetTarget(
const TargetType * ptr )
{
  this->ProcessObject::SetNthInput( 2, const_cast< TargetType * >( ptr ) );
}


/*
 * Get the target image.
 */
template <class TReference, class TTarget, class TDeformationField>
MultiResolutionPDEDeformableRegistration<TReference,TTarget,TDeformationField>
::TargetConstPointer
MultiResolutionPDEDeformableRegistration<TReference,TTarget,TDeformationField>
::GetTarget(void)
{
  return dynamic_cast< const TargetType * >
    ( this->ProcessObject::GetInput( 2 ).GetPointer() );
}


/*
 * Set the number of multi-resolution levels
 */
template <class TReference, class TTarget, class TDeformationField>
void
MultiResolutionPDEDeformableRegistration<TReference,TTarget,TDeformationField>
::SetNumberOfLevels(
unsigned int num )
{
  if( m_NumberOfLevels != num )
    {
    this->Modified();
    m_NumberOfLevels = num;
    m_NumberOfIterations.resize( m_NumberOfLevels );
    }

  if( m_ReferencePyramid && m_ReferencePyramid->GetNumberOfLevels() != num )
    {
    m_ReferencePyramid->SetNumberOfLevels( m_NumberOfLevels );
    }
  if( m_TargetPyramid && m_TargetPyramid->GetNumberOfLevels() != num )
    {
    m_TargetPyramid->SetNumberOfLevels( m_NumberOfLevels );
    }  
    
}


/*
 * Standard PrintSelf method.
 */
template <class TReference, class TTarget, class TDeformationField>
void
MultiResolutionPDEDeformableRegistration<TReference,TTarget,TDeformationField>
::PrintSelf(std::ostream& os, Indent indent) const
{
  Superclass::PrintSelf(os, indent);
  os << indent << "NumberOfLevels: " << m_NumberOfLevels << std::endl;
  os << indent << "CurrentLevel: " << m_CurrentLevel << std::endl;

  os << indent << "NumberOfIterations: [";
  unsigned int ilevel;
  for( ilevel = 0; ilevel < m_NumberOfLevels - 1; ilevel++ )
    {
    os << m_NumberOfIterations[ilevel] << ", ";
    }
  os << m_NumberOfIterations[ilevel] << "]" << std::endl;
  
  os << indent << "RegistrationFilter: ";
  os << m_RegistrationFilter.GetPointer() << std::endl;
  os << indent << "ReferencePyramid: ";
  os << m_ReferencePyramid.GetPointer() << std::endl;
  os << indent << "TargetPyramid: ";
  os << m_TargetPyramid.GetPointer() << std::endl;

}

/*
 * Perform a the deformable registration using a multiresolution scheme
 * using an internal mini-pipeline
 *
 *  ref_pyramid ->  registrator  ->  field_expander --|| tempField
 * test_pyramid ->           |                              |
 *                           |                              |
 *                           --------------------------------    
 *
 * A tempField image is used to break the cycle between the
 * registrator and field_expander.
 *
 */                              
template <class TReference, class TTarget, class TDeformationField>
void
MultiResolutionPDEDeformableRegistration<TReference,TTarget,TDeformationField>
::GenerateData()
{

  // allocate memory for the results
  DeformationFieldPointer outputPtr = this->GetOutput();
  outputPtr->SetBufferedRegion( outputPtr->GetRequestedRegion() );
  outputPtr->Allocate();

  // get a pointer to the reference and test images
  ReferenceConstPointer reference = this->GetReference();
  TargetConstPointer    target = this->GetTarget();

  if( !reference || !target )
    {
    itkExceptionMacro( << "Reference and/or Target not set" );
    }

  if( !m_ReferencePyramid || !m_TargetPyramid )
    {
    itkExceptionMacro( << "Reference and/or Target Pyramid not set" );
    }

  if( !m_RegistrationFilter )
    {
    itkExceptionMacro( << "Registration filter not set" );
    }
  
  // setup the filters
  m_ReferencePyramid->SetInput( reference );
  m_TargetPyramid->SetInput( target );
  m_FieldExpander->SetInput( m_RegistrationFilter->GetOutput() );


  unsigned int refLevel, targetLevel;
  int idim;
  unsigned int lastShrinkFactors[ImageDimension];
  unsigned int expandFactors[ImageDimension];

  DeformationFieldPointer tempField = DeformationFieldType::New();

  for( m_CurrentLevel = 0; m_CurrentLevel < m_NumberOfLevels; 
       m_CurrentLevel++ )
    {
   
    refLevel = vnl_math_min( (int) m_CurrentLevel, 
      (int) m_ReferencePyramid->GetNumberOfLevels() );
    targetLevel = vnl_math_min( (int) m_CurrentLevel, 
      (int) m_TargetPyramid->GetNumberOfLevels() );

    if( m_CurrentLevel == 0 )
      {
       /*
         * \todo What to do if there is an input deformation field?
         * Will need a VectorMultiResolutionPyramidImageFilter to downsample it.
         */
        //DeformationFieldPointer initialField = this->GetInput();
      m_RegistrationFilter->SetInitialDeformationField( NULL );
      }
    else
      {

      // compute the expand factors for upsampling the deformation field
      for( idim = 0; idim < ImageDimension; idim++ )
        {
        expandFactors[idim] = (unsigned int) ceil( 
         (float) lastShrinkFactors[idim] / 
         (float) m_TargetPyramid->GetSchedule()[targetLevel][idim] );
        if( expandFactors[idim] < 1 )
          {
          expandFactors[idim] = 1;
          }  
        }

      // graft a temporary image as the output of the expander
      // this is used to break the loop between the registrator
      // and expander
      m_FieldExpander->GraftOutput( tempField );
      m_FieldExpander->SetExpandFactors( expandFactors ); 
      m_FieldExpander->UpdateLargestPossibleRegion();

      tempField = m_FieldExpander->GetOutput();
      tempField->DisconnectPipeline();
      m_RegistrationFilter->SetInitialDeformationField( tempField );

      }

    this->UpdateProgress( (float) m_CurrentLevel / 
      (float) m_NumberOfLevels );

    // setup registration filter and pyramids 
    m_RegistrationFilter->SetReference( m_ReferencePyramid->GetOutput(refLevel) );
    m_RegistrationFilter->SetTarget( m_TargetPyramid->GetOutput(targetLevel) );

    m_RegistrationFilter->SetNumberOfIterations(
      m_NumberOfIterations[m_CurrentLevel] );

    // cache shrink factors for computing the next expand factors.
    for( idim = 0; idim < ImageDimension; idim++ )
      {
      lastShrinkFactors[idim] = 
        m_TargetPyramid->GetSchedule()[targetLevel][idim];
      }

    if( m_CurrentLevel < m_NumberOfLevels - 1)
      {

      // compute new deformation field
      m_RegistrationFilter->UpdateLargestPossibleRegion();

      }
    else
      {

      // this is the last level
      for( idim = 0; idim < ImageDimension; idim++ )
        {
        if ( lastShrinkFactors[idim] > 1 ) { break; }
        }
      if( idim < ImageDimension )
        {

        // some of the last shrink factors are not one
        // graft the output of the expander filter to
        // to output of this filter
        m_FieldExpander->GraftOutput( outputPtr );
        m_FieldExpander->SetExpandFactors( lastShrinkFactors );
        m_FieldExpander->UpdateLargestPossibleRegion();
        this->GraftOutput( m_FieldExpander->GetOutput() );

        }
      else
        {

        // all the last shrink factors are all ones
        // graft the output of registration filter to
        // to output of this filter
        m_RegistrationFilter->GraftOutput( outputPtr );
        m_RegistrationFilter->UpdateLargestPossibleRegion();
        this->GraftOutput( m_RegistrationFilter->GetOutput() );

        }
      
      } // end if m_CurrentLevel
    } // end m_CurrentLevel loop


   // Reset the m_CurrentLevel to zero
   m_CurrentLevel = 0;
}



template <class TReference, class TTarget, class TDeformationField>
void
MultiResolutionPDEDeformableRegistration<TReference,TTarget,TDeformationField>
::GenerateOutputInformation()
{

 typename DataObject::Pointer output;

 if( this->GetInput(0) )
  {
  // Initial deformation field is set.
  // Copy information from initial field.
  this->Superclass::GenerateOutputInformation();

  }
 else if( this->GetInput(2) )
  {
  // Initial deforamtion field is not set. 
  // Copy information from the target image.
  for (unsigned int idx = 0; idx < 
    this->GetNumberOfOutputs(); ++idx )
    {
    output = this->GetOutput(idx);
    if (output)
      {
      output->CopyInformation(this->GetInput(2));
      }  
    }

  }

}


template <class TReference, class TTarget, class TDeformationField>
void
MultiResolutionPDEDeformableRegistration<TReference,TTarget,TDeformationField>
::GenerateInputRequestedRegion()
{

  // call the superclass's implementation
  Superclass::GenerateInputRequestedRegion();

  // request the largest possible region for the reference, 
  // target and initial deformation field
  ReferencePointer refPtr = 
    const_cast< ReferenceType * >( this->GetReference().GetPointer() );
  if( refPtr )
    {
    refPtr->SetRequestedRegionToLargestPossibleRegion();
    }
  
  TargetPointer targetPtr = 
    const_cast< TargetType * >( this->GetTarget().GetPointer() );
  if( targetPtr )
    {
    targetPtr->SetRequestedRegionToLargestPossibleRegion();
    }

  DeformationFieldPointer inputPtr = 
    const_cast< DeformationFieldType * >( this->GetInput().GetPointer() );
  if( inputPtr )
    {
    inputPtr->SetRequestedRegionToLargestPossibleRegion();
    }

}


template <class TReference, class TTarget, class TDeformationField>
void
MultiResolutionPDEDeformableRegistration<TReference,TTarget,TDeformationField>
::EnlargeOutputRequestedRegion(
DataObject * ptr )
{
  // call the superclass's implementation
  Superclass::EnlargeOutputRequestedRegion( ptr );

  // set the output requested region to largest possible.
  DeformationFieldType * outputPtr;
  outputPtr = dynamic_cast<DeformationFieldType*>( ptr );

  if( outputPtr )
    {
    outputPtr->SetRequestedRegionToLargestPossibleRegion();
    }

}


} // end namespace itk

#endif
