/*=========================================================================
 *
 *  Copyright Insight Software Consortium
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/
#ifndef __itkParametricGaussianImageSource_txx
#define __itkParametricGaussianImageSource_txx

#include "itkParametricGaussianImageSource.h"

namespace itk
{

template <class TOutputImage>
ParametricGaussianImageSource<TOutputImage>
::ParametricGaussianImageSource()
{
  this->m_GaussianImageSource = GaussianImageSourceType::New();
}

template <class TOutputImage>
ParametricGaussianImageSource<TOutputImage>
::~ParametricGaussianImageSource()
{
}


template<typename TOutputImage>
void
ParametricGaussianImageSource<TOutputImage>
::SetSigma(const ArrayType& sigma)
{
  if (sigma != this->m_GaussianImageSource->GetSigma())
    {
    this->m_GaussianImageSource->SetSigma(sigma);
    this->Modified();
    }
}


template< typename TOutputImage >
const typename ParametricGaussianImageSource< TOutputImage >::ArrayType&
ParametricGaussianImageSource< TOutputImage >
::GetSigma() const
{
  return this->m_GaussianImageSource->GetSigma();
}


template< typename TOutputImage >
void
ParametricGaussianImageSource< TOutputImage >
::SetMean(const ArrayType& mean)
{
  if (mean != this->m_GaussianImageSource->GetMean())
    {
    this->m_GaussianImageSource->SetMean(mean);
    this->Modified();
    }
}


template< typename TOutputImage >
const typename ParametricGaussianImageSource< TOutputImage >::ArrayType&
ParametricGaussianImageSource< TOutputImage >
::GetMean() const
{
  return this->m_GaussianImageSource->GetMean();
}


template< typename TOutputImage >
void
ParametricGaussianImageSource< TOutputImage >
::SetScale(double scale)
{
  if (scale != this->m_GaussianImageSource->GetScale())
    {
    this->m_GaussianImageSource->SetScale(scale);
    this->Modified();
    }
}


template< typename TOutputImage >
double
ParametricGaussianImageSource< TOutputImage >
::GetScale() const
{
  return this->m_GaussianImageSource->GetScale();
}


template<typename TOutputImage>
void
ParametricGaussianImageSource<TOutputImage>
::SetParameter(unsigned int index, ParametersValueType value)
{
  const unsigned int dimensions = itkGetStaticConstMacro(OutputImageDimension);
  if (index >= 0 && index < dimensions)
    {
    ArrayType sigma = this->GetSigma();
    sigma[index] = value;
    this->SetSigma(sigma);
    }
  else if (index == dimensions)
    {
    this->SetScale(value);
    }
}


template<typename TOutputImage>
typename ParametricGaussianImageSource<TOutputImage>::ParametersValueType
ParametricGaussianImageSource<TOutputImage>
::GetParameter(unsigned int index) const
{
  const unsigned int dimensions = itkGetStaticConstMacro(OutputImageDimension);
  if (index >= 0 && index < dimensions)
    {
    return this->GetSigma()[index];
    }
  else if (index == dimensions)
    {
    return this->GetScale();
    }

  return 0.0;
}


template<typename TOutputImage>
void
ParametricGaussianImageSource<TOutputImage>
::SetParameters(const ParametersType& parameters)
{
  const unsigned int dimensions = itkGetStaticConstMacro(OutputImageDimension);
  ArrayType sigma;
  for ( unsigned int i=0; i<dimensions; i++)
    {
    sigma[i] = parameters[i];
    }
  this->SetSigma(sigma);
  this->SetScale(parameters[dimensions]);
}


template<typename TOutputImage>
typename ParametricGaussianImageSource<TOutputImage>::ParametersType
ParametricGaussianImageSource<TOutputImage>
::GetParameters() const
{
  ParametersType parameters(GetNumberOfParameters());
  ArrayType sigma = this->GetSigma();

  const unsigned int dimensions = itkGetStaticConstMacro(OutputImageDimension);
  for (unsigned int i=0; i<dimensions; i++)
    {
    parameters[i] = sigma[i];
    }
  parameters[dimensions] = this->GetScale();

  return parameters;
}


template<typename TOutputImage>
unsigned int
ParametricGaussianImageSource<TOutputImage>
::GetNumberOfParameters() const
{
  return itkGetStaticConstMacro(OutputImageDimension) + 1;
}


template<typename TOutputImage>
void
ParametricGaussianImageSource<TOutputImage>
::GenerateData()
{
  m_GaussianImageSource->SetSize(this->m_Size);
  m_GaussianImageSource->SetSpacing(this->m_Spacing);
  m_GaussianImageSource->SetOrigin(this->m_Origin);

  m_GaussianImageSource->GraftOutput(this->GetOutput());

  m_GaussianImageSource->UpdateLargestPossibleRegion();

  this->GraftOutput(m_GaussianImageSource->GetOutput());
}


template <class TOutputImage>
void
ParametricGaussianImageSource<TOutputImage>
::PrintSelf(std::ostream& os, Indent indent) const
{
  Superclass::PrintSelf(os,indent);

  std::cout << this->m_GaussianImageSource << std::endl;
}


} // end namespace itk

#endif
