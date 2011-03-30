#ifndef _DATA_MODEL_CXX_
#define _DATA_MODEL_CXX_

#if defined(_WIN32) // Turn off deprecation warnings in Visual Studio
#pragma warning( disable : 4996 )
#endif

#include <cfloat>
#include <cstdlib>

#include <itkMultiThreader.h>
#include <itkPoint.h>
#include <itkImageFileWriter.h>

#include "Validation.h"

#ifdef VALIDATE_CONVOLUTION
#include <itkBeadSpreadFunctionImageSource2.txx>
#else
#include <itkBeadSpreadFunctionImageSource.txx>
#endif

#include <itkGaussianPointSpreadFunctionImageSource.txx>
#include <itkGibsonLanniPointSpreadFunctionImageSource.txx>
#include <itkModifiedGibsonLanniPointSpreadFunctionImageSource.txx>
#include <itkGridImageSource.txx>
#include <itkImageFileReader.txx>
#include <itkImageFileWriter.txx>
#include <itkImageToParametricImageSourceMetric.txx>
#include <itkMeanSquaresImageToImageMetric.txx>
#include <itkMinimumMaximumImageCalculator.txx>
//#include <itkPoissonNoiseImageToImageMetric.cxx>
#include <itkShiftScaleImageFilter.txx>
#include <itkSubtractImageFilter.h>

#include <ITKImageToVTKImage.cxx>

#include <vtkAlgorithm.h>

#include "DataModel.h"
#include "StringUtils.h"


DataModel
::DataModel() {
  // ITK will detect the number of cores on the system and set the
  // global number of threads to the number of cores by default.
  // Here we can override that setting if the proper environment
  // variable is set.
  char *var = getenv("PSFEstimator_THREADS");
  if (var) {
    int numberOfThreads = atoi(var);
    if (numberOfThreads > 0)
      SetNumberOfThreads(numberOfThreads);
  }


  m_MeasuredImageData = NULL;

  m_GaussianPSFSource                  = GaussianPSFImageSourceType::New();
  m_GaussianPSFKernelSource            = GaussianPSFImageSourceType::New();
  m_GibsonLanniPSFSource               = GibsonLanniPSFImageSourceType::New();
  m_GibsonLanniPSFKernelSource         = GibsonLanniPSFImageSourceType::New();
  m_ModifiedGibsonLanniPSFSource       = ModifiedGibsonLanniPSFImageSourceType::New();
  m_ModifiedGibsonLanniPSFKernelSource = ModifiedGibsonLanniPSFImageSourceType::New();
  m_BeadSpreadFunctionSource           = BeadSpreadFunctionImageSourceType::New();

  m_BSFDifferenceImageFilter         = DifferenceFilterType::New();

  m_MeasuredImageMinMaxFilter        = MinMaxType::New();
  m_PSFImageMinMaxFilter             = MinMaxType::New();
  m_BSFImageMinMaxFilter             = MinMaxType::New();
  m_BSFDifferenceImageMinMaxFilter   = MinMaxType::New();

  m_MeasuredImageITKToVTKFilter      = new ITKImageToVTKImage<TImage>();
  m_PSFImageITKToVTKFilter           = new ITKImageToVTKImage<TImage>();
  m_BSFImageITKToVTKFilter           = new ITKImageToVTKImage<TImage>();
  m_BSFDifferenceImageITKToVTKFilter = new ITKImageToVTKImage<TImage>();

  m_ImageToImageCostFunction = ImageToImageCostFunctionType::New();
  m_CostFunction = ParametricCostFunctionType::New();
  m_CostFunction->SetInterpolator(InterpolatorType::New());
  m_CostFunction->SetDelegateMetric(m_ImageToImageCostFunction);

  // Default to Gibson-Lanni PSF type.
  SetPointSpreadFunctionType(GIBSON_LANNI_PSF);

  Initialize();
  InitializeParameterScales(GetPointSpreadFunctionType());
}


DataModel
::~DataModel() {
  delete m_MeasuredImageITKToVTKFilter;
  delete m_PSFImageITKToVTKFilter;
  delete m_BSFImageITKToVTKFilter;
  delete m_BSFDifferenceImageITKToVTKFilter;
}


void
DataModel
::SetPointSpreadFunctionType(PointSpreadFunctionType psfType) {
  if (psfType == m_PointSpreadFunctionType)
    return;

  m_PointSpreadFunctionType = psfType;

  switch (psfType) {
  case GAUSSIAN_PSF:
    m_BeadSpreadFunctionSource->SetKernelSource(m_GaussianPSFKernelSource);
#ifndef VALIDATE_CONVOLUTION
    m_BeadSpreadFunctionSource->SetKernelIsRadiallySymmetric(false);
#endif
    m_PointSpreadFunctionSource = m_GaussianPSFSource;;
    break;

  case GIBSON_LANNI_PSF:
    m_BeadSpreadFunctionSource->SetKernelSource(m_GibsonLanniPSFKernelSource);
    m_BeadSpreadFunctionSource->SetKernelIsRadiallySymmetric(false);
    m_PointSpreadFunctionSource = m_GibsonLanniPSFSource;
    break;

  case MODIFIED_GIBSON_LANNI_PSF:
    m_BeadSpreadFunctionSource->SetKernelSource(m_ModifiedGibsonLanniPSFKernelSource);
    m_BeadSpreadFunctionSource->SetKernelIsRadiallySymmetric(false);
    m_PointSpreadFunctionSource = m_ModifiedGibsonLanniPSFSource;
    break;

  case HAEBERLE_PSF:
#ifndef VALIDATE_CONVOLUTION
    m_BeadSpreadFunctionSource->SetKernelIsRadiallySymmetric(true);
#endif
    // TODO - plugin Haeberle PSF source here
    break;

  }

  InitializeParameterScales(psfType);

  m_CostFunction->SetMovingImageSource(m_BeadSpreadFunctionSource);

  m_PSFImageMinMaxFilter->SetImage(m_PointSpreadFunctionSource->GetOutput());
  m_PSFImageITKToVTKFilter->SetInput(m_PointSpreadFunctionSource->GetOutput());
  if (m_MeasuredImageData) {
    double spacing[3];
    GetMeasuredImageVoxelSpacing(spacing);
    SetPSFImageVoxelSpacing(spacing);

    int size[3];
    GetMeasuredImageDimensions(size);
    SetPSFImageDimensions(size);

    RecenterImageOrigin();
  }
}


DataModel::PointSpreadFunctionType
DataModel
::GetPointSpreadFunctionType() const {
  return m_PointSpreadFunctionType;
}


bool
DataModel
::LoadSessionFile(const std::string& fileName) {
  Configuration config;
  config.Parse(fileName);

  // Read the settings from the configuration structure
  std::string imageFileName = config.GetValue("FileInfo", "FileName");
  if (imageFileName.compare("")) {
    bool success = LoadImageFile(imageFileName);
    if (!success) {
      return false;
    }
  } else {
    CreateImageFile(config.GetValueAsInt("FileInfo", "SizeX"),
                    config.GetValueAsInt("FileInfo", "SizeY"),
                    config.GetValueAsInt("FileInfo", "SizeZ"),
                    config.GetValueAsDouble("BeadSpreadFunctionSettings", "XPixelSize"),
                    config.GetValueAsDouble("BeadSpreadFunctionSettings", "YPixelSize"),
                    config.GetValueAsDouble("BeadSpreadFunctionSettings", "ZSliceSpacing"));
  }

  SetConfiguration(config);

  return true;
}


bool
DataModel
::SaveSessionFile(const std::string& fileName) {
  Configuration config;
  GetConfiguration(config);
  std::ofstream os(fileName.c_str());
  config.Write(os);

  return true;
}


void
DataModel
::Initialize() {
  // BSF parameters
  m_BSFParameterNames.clear();
  m_BSFParameterUnits.clear();
  m_BSFParameterMask.clear();
  m_BSFParameterNames.push_back("X Pixel Size");
  m_BSFParameterUnits.push_back("nanometers");
  m_BSFParameterNames.push_back("Y Pixel Size");
  m_BSFParameterUnits.push_back("nanometers");
  m_BSFParameterNames.push_back("Z Slice Spacing");
  m_BSFParameterUnits.push_back("nanometers");
  m_BSFParameterNames.push_back("Bead Radius");
  m_BSFParameterUnits.push_back("nanometers");
  m_BSFParameterNames.push_back("Bead Center X");
  m_BSFParameterUnits.push_back("nanometers");
  m_BSFParameterNames.push_back("Bead Center Y");
  m_BSFParameterUnits.push_back("nanometers");
  m_BSFParameterNames.push_back("Bead Center Z");
  m_BSFParameterUnits.push_back("nanometers");
  m_BSFParameterNames.push_back("Shear X");
  m_BSFParameterUnits.push_back("nanometers in X vs. nanometers in Z");
  m_BSFParameterNames.push_back("Shear Y");
  m_BSFParameterUnits.push_back("nanometers in Y vs. nanometers in Z");
  m_BSFParameterNames.push_back("Intensity Shift");
  m_BSFParameterUnits.push_back("-");
  m_BSFParameterNames.push_back("Intensity Scale");
  m_BSFParameterUnits.push_back("-");

  typedef std::vector<bool>::size_type SizeType;
  for ( SizeType i = 0; i < m_BSFParameterNames.size(); i++) {
    m_BSFParameterMask.push_back(false);
  }

  // Gaussian parameters
  m_GaussianPSFParameterNames.clear();
  m_GaussianPSFParameterNames.push_back("Standard Deviation X");
  m_GaussianPSFParameterUnits.push_back("nanometers");
  m_GaussianPSFParameterNames.push_back("Standard Deviation Y");
  m_GaussianPSFParameterUnits.push_back("nanometers");
  m_GaussianPSFParameterNames.push_back("Standard Deviation Z");
  m_GaussianPSFParameterUnits.push_back("nanometers");

  for ( SizeType i = 0; i < m_GaussianPSFParameterNames.size(); i++) {
    m_GaussianPSFParameterMask.push_back(false);
  }


  // OPD-based PSF parameters
  m_OPDBasedPSFParameterNames.clear();
  m_OPDBasedPSFParameterNames.push_back("Emission Wavelength");
  m_OPDBasedPSFParameterUnits.push_back("nanometers");
  m_OPDBasedPSFParameterNames.push_back("Numerical Aperture");
  m_OPDBasedPSFParameterUnits.push_back("-");
  m_OPDBasedPSFParameterNames.push_back("Magnification");
  m_OPDBasedPSFParameterUnits.push_back("-");
  m_OPDBasedPSFParameterNames.push_back("Design Cover Slip Refractive Index");
  m_OPDBasedPSFParameterUnits.push_back("-");
  m_OPDBasedPSFParameterNames.push_back("Actual Cover Slip Refractive Index");
  m_OPDBasedPSFParameterUnits.push_back("-");
  m_OPDBasedPSFParameterNames.push_back("Design Cover Slip Thickness");
  m_OPDBasedPSFParameterUnits.push_back("micrometers");
  m_OPDBasedPSFParameterNames.push_back("Actual Cover Slip Thickness");
  m_OPDBasedPSFParameterUnits.push_back("micrometers");
  m_OPDBasedPSFParameterNames.push_back("Design Immersion Oil Refractive Index");
  m_OPDBasedPSFParameterUnits.push_back("-");
  m_OPDBasedPSFParameterNames.push_back("Actual Immersion Oil Refractive Index");
  m_OPDBasedPSFParameterUnits.push_back("-");
  m_OPDBasedPSFParameterNames.push_back("Design Immersion Oil Thickness");
  m_OPDBasedPSFParameterUnits.push_back("micrometers");
  m_OPDBasedPSFParameterNames.push_back("Design Specimen Layer Refractive Index");
  m_OPDBasedPSFParameterUnits.push_back("-");
  m_OPDBasedPSFParameterNames.push_back("Actual Specimen Layer Refractive Index");
  m_OPDBasedPSFParameterUnits.push_back("-");
  m_OPDBasedPSFParameterNames.push_back("Actual Point Source Depth in Specimen Layer");
  m_OPDBasedPSFParameterUnits.push_back("micrometers");
  m_OPDBasedPSFParameterNames.push_back("PSF Shear X");
  m_OPDBasedPSFParameterUnits.push_back("nanometers in X vs. nanometers in Z");
  m_OPDBasedPSFParameterNames.push_back("PSF Shear Y");
  m_OPDBasedPSFParameterUnits.push_back("nanometers in Y vs. nanometers in Z");

  for ( SizeType i = 0; i < m_OPDBasedPSFParameterNames.size(); i++) {
    m_GibsonLanniPSFParameterMask.push_back(false);
  }


  // Modified Gibson-Lanni PSF parameters
  m_ModifiedGibsonLanniPSFParameterNames = m_OPDBasedPSFParameterNames;
  m_ModifiedGibsonLanniPSFParameterUnits = m_OPDBasedPSFParameterUnits;
  m_ModifiedGibsonLanniPSFParameterNames.push_back("Gaussian Center X");
  m_ModifiedGibsonLanniPSFParameterUnits.push_back("nanometers");
  m_ModifiedGibsonLanniPSFParameterNames.push_back("Gaussian Center Y");
  m_ModifiedGibsonLanniPSFParameterUnits.push_back("nanometers");
  m_ModifiedGibsonLanniPSFParameterNames.push_back("Gaussian Center Z");
  m_ModifiedGibsonLanniPSFParameterUnits.push_back("nanometers");
  m_ModifiedGibsonLanniPSFParameterNames.push_back("Gaussian Sigma X");
  m_ModifiedGibsonLanniPSFParameterUnits.push_back("nanometers");
  m_ModifiedGibsonLanniPSFParameterNames.push_back("Gaussian Sigma Y");
  m_ModifiedGibsonLanniPSFParameterUnits.push_back("nanometers");
  m_ModifiedGibsonLanniPSFParameterNames.push_back("Gaussian Sigma Z");
  m_ModifiedGibsonLanniPSFParameterUnits.push_back("nanometers");
  m_ModifiedGibsonLanniPSFParameterNames.push_back("Gaussian Intensity Scale");
  m_ModifiedGibsonLanniPSFParameterUnits.push_back("-");

  for ( SizeType i = 0; i < m_ModifiedGibsonLanniPSFParameterNames.size(); i++) {
    m_ModifiedGibsonLanniPSFParameterMask.push_back(false);
  }

}


void
DataModel
::InitializeParameterScales(PointSpreadFunctionType psfType) {
  m_ParameterScales.clear();
  int numParameters = m_BeadSpreadFunctionSource->GetNumberOfParameters();
  m_ParameterScales.SetSize(numParameters);

  // BSF parameter scales
  int index = 0;
  m_ParameterScales[index++] = 1.0; // X-spacing
  m_ParameterScales[index++] = 1.0; // Y-spacing
  m_ParameterScales[index++] = 1.0; // Z-spacing
  m_ParameterScales[index++] = 10.0; // Bead radius
  m_ParameterScales[index++] = 100.0; // Bead center X
  m_ParameterScales[index++] = 100.0; // Bead center Y
  m_ParameterScales[index++] = 100.0; // Bead center Z
  m_ParameterScales[index++] = 0.1; // Shear X
  m_ParameterScales[index++] = 0.1; // Shear Y
  m_ParameterScales[index++] = 1.0;   // Intensity shift
  m_ParameterScales[index++] = 1.0;   // Intensity scale

  // PSF parameter scales
  switch (psfType) {

  case GAUSSIAN_PSF:
    m_ParameterScales[index++] = 5.0;
    m_ParameterScales[index++] = 5.0;
    m_ParameterScales[index++] = 5.0;
    break;

  case GIBSON_LANNI_PSF:
  case MODIFIED_GIBSON_LANNI_PSF:
  case HAEBERLE_PSF:
    m_ParameterScales[index++] = 5.0;   // Emission wavelength
    m_ParameterScales[index++] = 0.05;  // NA
    m_ParameterScales[index++] = 1.0;   // Magnification
    m_ParameterScales[index++] = 0.001; // Design cover slip RI
    m_ParameterScales[index++] = 0.001; // Actual cover slip RI
    m_ParameterScales[index++] = 1.0;   // Design cover slip thickness
    m_ParameterScales[index++] = 1.0;   // Actual cover slip thickness
    m_ParameterScales[index++] = 0.001; // Design immersion oil RI
    m_ParameterScales[index++] = 0.001; // Actual immersion oil RI
    m_ParameterScales[index++] = 1.0;   // Design immersion oil thickness
    m_ParameterScales[index++] = 0.001; // Design specimen layer RI
    m_ParameterScales[index++] = 0.001; // Actual specimen layer RI
    m_ParameterScales[index++] = 1.0;   // Actual point source depth
    m_ParameterScales[index++] = 0.01;  // Shear X
    m_ParameterScales[index++] = 0.01;  // Shear Y
    break;

  default:
    break;
  }

  if (psfType == MODIFIED_GIBSON_LANNI_PSF) {
    m_ParameterScales[index++] = 10.0;
    m_ParameterScales[index++] = 10.0;
    m_ParameterScales[index++] = 10.0;
    m_ParameterScales[index++] = 10.0;
    m_ParameterScales[index++] = 10.0;
    m_ParameterScales[index++] = 10.0;
    m_ParameterScales[index++] = 0.1;
  }
}


void
DataModel
::CreateImageFile(int xSize, int ySize, int zSize,
                  double xSpacing, double ySpacing, double zSpacing) {

  DummyImageSourcePointer dummy = DummyImageSourceType::New();

  DummyImageSourceType::SizeType dummySize;
  dummySize[0] = xSize;
  dummySize[1] = ySize;
  dummySize[2] = zSize;
  dummy->SetSize(dummySize);

  DummyImageSourceType::SpacingType dummySpacing;
  dummySpacing[0] = xSpacing;
  dummySpacing[1] = ySpacing;
  dummySpacing[2] = zSpacing;
  dummy->SetSpacing(dummySpacing);

  dummy->SetScale(0.0);
  dummy->Update();
  SetMeasuredImageData(dummy->GetOutput());

  // Connect this image data to the various pipelines.
  m_MeasuredImageMinMaxFilter->SetImage(m_MeasuredImageData);
  Float3DImageType::RegionType region =
    m_MeasuredImageData->GetLargestPossibleRegion();
  m_MeasuredImageMinMaxFilter->SetRegion(region);
  m_MeasuredImageMinMaxFilter->Compute();

  m_MeasuredImageITKToVTKFilter->Modified();
  m_MeasuredImageITKToVTKFilter->Update();

  // Set up PSF settings to match loaded image settings
  double spacing[3];
  GetMeasuredImageVoxelSpacing(spacing);
  SetPSFImageVoxelSpacing(spacing);
  SetBSFImageVoxelSpacing(spacing);

  int size[3];
  GetMeasuredImageDimensions(size);
  SetPSFImageDimensions(size);
  SetBSFImageDimensions(size);

  PointType origin;
  for (int i = 0; i < 3; i++)
    origin[i] = -spacing[i]*static_cast<double>(size[i])*0.5;
  m_PointSpreadFunctionSource->SetOrigin(origin);
  m_BeadSpreadFunctionSource->SetOrigin(origin);
  m_MeasuredImageData->SetOrigin(origin);

  m_PSFImageMinMaxFilter->SetImage(m_PointSpreadFunctionSource->GetOutput());
  m_PSFImageITKToVTKFilter->SetInput(m_PointSpreadFunctionSource->GetOutput());

  m_BSFImageMinMaxFilter->SetImage(m_BeadSpreadFunctionSource->GetOutput());
  m_BSFImageITKToVTKFilter->SetInput(m_BeadSpreadFunctionSource->GetOutput());

  // Set up cost function
  m_CostFunction->SetFixedImage(m_MeasuredImageData);
}


bool
DataModel
::LoadImageFile(std::string fileName) {
  m_ImageFileName = fileName;
  ScalarFileReaderType::Pointer reader = ScalarFileReaderType::New();
  reader->SetFileName(fileName.c_str());

  try {
    reader->Update();
  } catch (...) {
    return false;
  }
  SetMeasuredImageData(reader->GetOutput());

  // Connect this image data to the various pipelines.
  m_MeasuredImageMinMaxFilter->SetImage(m_MeasuredImageData);
  Float3DImageType::RegionType region =
    m_MeasuredImageData->GetLargestPossibleRegion();
  m_MeasuredImageMinMaxFilter->SetRegion(region);
  m_MeasuredImageMinMaxFilter->Compute();

  m_MeasuredImageITKToVTKFilter->Modified();
  m_MeasuredImageITKToVTKFilter->Update();

  // Set up PSF settings to match loaded image settings
  double spacing[3];
  GetMeasuredImageVoxelSpacing(spacing);
  SetPSFImageVoxelSpacing(spacing);
  SetBSFImageVoxelSpacing(spacing);

  int size[3];
  GetMeasuredImageDimensions(size);
  SetPSFImageDimensions(size);
  SetBSFImageDimensions(size);

  PointType origin;
  for (int i = 0; i < 3; i++)
    origin[i] = -spacing[i]*static_cast<double>(size[i])*0.5;
  m_PointSpreadFunctionSource->SetOrigin(origin);
  m_BeadSpreadFunctionSource->SetOrigin(origin);
  m_MeasuredImageData->SetOrigin(origin);

  m_PSFImageMinMaxFilter->SetImage(m_PointSpreadFunctionSource->GetOutput());
  m_PSFImageITKToVTKFilter->SetInput(m_PointSpreadFunctionSource->GetOutput());

  m_BSFImageMinMaxFilter->SetImage(m_BeadSpreadFunctionSource->GetOutput());
  m_BSFImageITKToVTKFilter->SetInput(m_BeadSpreadFunctionSource->GetOutput());

  m_BSFDifferenceImageFilter->SetInput1(m_BeadSpreadFunctionSource->GetOutput());
  m_BSFDifferenceImageFilter->SetInput2(m_MeasuredImageData);

  m_BSFDifferenceImageMinMaxFilter->SetImage(m_BSFDifferenceImageFilter->GetOutput());
  m_BSFDifferenceImageITKToVTKFilter->SetInput(m_BSFDifferenceImageFilter->GetOutput());

  // Set up cost function
  m_CostFunction->SetFixedImage(m_MeasuredImageData);
  m_CostFunction->SetMovingImageSource(m_BeadSpreadFunctionSource);

  return true;
}


void
DataModel
::SavePSFImageFile(std::string fileName) {
  TIFFScaleType::Pointer scaler = TIFFScaleType::New();
  scaler->SetInput(m_PointSpreadFunctionSource->GetOutput());
  double min = GetPSFImageDataMinimum();
  double max = GetPSFImageDataMaximum();
  scaler->SetShift(-min);
  scaler->SetScale(65535.0f / (max - min));

  TIFFWriterType::Pointer writer = TIFFWriterType::New();
  writer->SetFileName(fileName.c_str());
  writer->SetInput(scaler->GetOutput());
  writer->Update();
}


void
DataModel
::SaveBSFImageFile(std::string fileName) {
  TIFFScaleType::Pointer scaler = TIFFScaleType::New();
  scaler->SetInput(m_BeadSpreadFunctionSource->GetOutput());
  //double min = GetBSFImageDataMinimum();
  //double max = GetBSFImageDataMaximum();
  //scaler->SetShift(-min);
  //scaler->SetScale(65535.0f / (max - min));
  scaler->SetShift(0.0);
  scaler->SetScale(1.0);

  TIFFWriterType::Pointer writer = TIFFWriterType::New();
  writer->SetFileName(fileName.c_str());
  writer->SetInput(scaler->GetOutput());
  writer->Update();
}


void
DataModel
::SetConfiguration(Configuration & c) {
  std::string sec = std::string("BeadSpreadFunctionSettings");

  // Set the BSF parameter values
  unsigned int numBSFParameters = m_BeadSpreadFunctionSource->
    GetNumberOfBeadSpreadFunctionParameters();
  for (unsigned int i = 0; i < numBSFParameters; i++)
  {
    std::string paramName = SqueezeString(GetParameterName(i));
    SetParameterValue(i, c.GetValueAsDouble(sec, paramName,
                                            GetParameterValue(i)));
    m_BSFParameterMask[i] =
      c.GetValueAsBool(sec, paramName + "-Optimize", m_BSFParameterMask[i]);
  }

  std::string modelName = c.GetValue(sec, "PointSpreadFunctionModel");
  if (!modelName.compare("Gaussian")) {
    SetPointSpreadFunctionType(GAUSSIAN_PSF);
  } else if (!modelName.compare("ModifiedGibsonLanni")) {
    SetPointSpreadFunctionType(MODIFIED_GIBSON_LANNI_PSF);
  } else if (!modelName.compare("Haeberle")) {
    SetPointSpreadFunctionType(HAEBERLE_PSF);
  } else { // GibsonLanni
    SetPointSpreadFunctionType(GIBSON_LANNI_PSF);
  }

  // Set the PSF parameter values for the Gaussian model
  sec = std::string("GaussianModelSettings");
  for (unsigned int i = 0; i < m_GaussianPSFSource->GetNumberOfParameters(); i++)
  {
    std::string paramName = SqueezeString(m_GaussianPSFParameterNames[i]);
    double value = c.GetValueAsDouble(sec, paramName, m_GaussianPSFSource->GetParameter(i));
    m_GaussianPSFSource->SetParameter(i, value);
    m_GaussianPSFKernelSource->SetParameter(i, value);
    m_GaussianPSFParameterMask[i] =
      c.GetValueAsBool(sec, paramName + "-Optimize",
                       m_GaussianPSFParameterMask[i]);
  }

  // Get the PSF parameter values for the Gibson-Lanni model
  sec = std::string("GibsonLanniModelSettings");
  for (unsigned int i = 0; i < m_GibsonLanniPSFSource->GetNumberOfParameters(); i++)
  {
    std::string paramName = SqueezeString(m_OPDBasedPSFParameterNames[i]);
    double value = c.GetValueAsDouble(sec, paramName,
                                      m_GibsonLanniPSFSource->GetParameter(i));
    m_GibsonLanniPSFSource->SetParameter(i, value);
    m_GibsonLanniPSFKernelSource->SetParameter(i, value);
    m_GibsonLanniPSFParameterMask[i] =
      c.GetValueAsBool(sec, paramName + "-Optimize",
                       m_GibsonLanniPSFParameterMask[i]);
  }

  // Get the PSF parameter values for the modified Gibson-Lanni model
  sec = std::string("ModifiedGibsonLanniModelSettings");
  for (unsigned int i = 0; i < m_ModifiedGibsonLanniPSFSource->GetNumberOfParameters(); i++)
  {
    std::string paramName = SqueezeString(m_ModifiedGibsonLanniPSFParameterNames[i]);
    double value = c.GetValueAsDouble(sec, paramName,
                                      m_ModifiedGibsonLanniPSFSource->GetParameter(i));
    m_ModifiedGibsonLanniPSFSource->SetParameter(i, value);
    m_ModifiedGibsonLanniPSFKernelSource->SetParameter(i, value);
    m_ModifiedGibsonLanniPSFParameterMask[i] =
      c.GetValueAsBool(sec, paramName + "-Optimize",
                       m_ModifiedGibsonLanniPSFParameterMask[i]);
  }

  sec = std::string("ZSliceCoordinates");

  SetUseCustomZCoordinates(c.GetValueAsBool(sec, "UseCustomZCoordinates",
                                            GetUseCustomZCoordinates()));

  for (unsigned int i = 0; i < m_BeadSpreadFunctionSource->GetSize()[2]; i++) {
    char name[128];
    sprintf(name, "ZCoordinate%03d", i);
    SetZCoordinate(i, c.GetValueAsDouble(sec, name));
  }

}


void
DataModel
::GetConfiguration(Configuration & c) {
  // Dump the settings into the configuration structure
  std::string sec("FileInfo");
  c.SetValue(sec, "FileName", m_ImageFileName);
  int dims[3];
  GetBSFImageDimensions(dims);
  c.SetValueFromInt(sec, "SizeX", dims[0]);
  c.SetValueFromInt(sec, "SizeY", dims[1]);
  c.SetValueFromInt(sec, "SizeZ", dims[2]);

  sec = std::string("BeadSpreadFunctionSettings");

  // Get the BSF parameter values
  unsigned int numBSFParameters = m_BeadSpreadFunctionSource->
    GetNumberOfBeadSpreadFunctionParameters();
  for (unsigned int i = 0; i < numBSFParameters; i++)
  {
    std::string paramName = SqueezeString(GetParameterName(i));
    c.SetValueFromDouble(sec, paramName, GetParameterValue(i));
    c.SetValueFromBool(sec, paramName + "-Optimize", m_BSFParameterMask[i]);
  }

  std::string modelName;
  switch (GetPointSpreadFunctionType()) {
  case GAUSSIAN_PSF:
    modelName = "Gaussian";
    break;

  case GIBSON_LANNI_PSF:
    modelName = "GibsonLanni";
    break;

  case MODIFIED_GIBSON_LANNI_PSF:
    modelName = "ModifiedGibsonLanni";
    break;

  case HAEBERLE_PSF:
    modelName = "Haeberle";
    break;

  default:
    break;
  }

  c.SetValue(sec, "PointSpreadFunctionModel", modelName);

  // Get the PSF parameter values for the Gaussian model
  sec = std::string("GaussianModelSettings");
  for (unsigned int i = 0; i < m_GaussianPSFKernelSource->GetNumberOfParameters(); i++)
  {
    std::string paramName = SqueezeString(m_GaussianPSFParameterNames[i]);
    c.SetValueFromDouble(sec, paramName, m_GaussianPSFKernelSource->GetParameters()[i]);
    c.SetValueFromBool(sec, paramName + "-Optimize",
                       m_GaussianPSFParameterMask[i]);
  }

  // Get the PSF parameter values for the Gibson-Lanni model
  sec = std::string("GibsonLanniModelSettings");
  for (unsigned int i = 0; i < m_GibsonLanniPSFKernelSource->GetNumberOfParameters(); i++)
  {
    std::string paramName = SqueezeString(m_OPDBasedPSFParameterNames[i]);
    c.SetValueFromDouble(sec, paramName, m_GibsonLanniPSFKernelSource->GetParameters()[i]);
    c.SetValueFromBool(sec, paramName + "-Optimize",
                       m_GibsonLanniPSFParameterMask[i]);
  }

  // Get the PSF parameter values for the modified Gibson-Lanni model
  sec = std::string("ModifiedGibsonLanniModelSettings");
  for (unsigned int i = 0; i < m_ModifiedGibsonLanniPSFKernelSource->GetNumberOfParameters(); i++)
  {
    std::string paramName = SqueezeString(m_ModifiedGibsonLanniPSFParameterNames[i]);
    c.SetValueFromDouble(sec, paramName, m_ModifiedGibsonLanniPSFSource->GetParameters()[i]);
    c.SetValueFromBool(sec, paramName + "-Optimize",
                       m_ModifiedGibsonLanniPSFParameterMask[i]);
  }

  // TODO - Get the PSF parameter values for the Haeberle model

  sec = std::string("ZSliceCoordinates");

  c.SetValueFromBool(sec, "UseCustomZCoordinates", GetUseCustomZCoordinates());

  for (unsigned int i = 0; i < m_BeadSpreadFunctionSource->GetSize()[2]; i++) {
    char name[128];
    sprintf(name, "ZCoordinate%03d", i);
    c.SetValueFromDouble(sec, name, GetZCoordinate(i));
  }

}


std::string
DataModel
::GetMeasuredImageFileName() {
  return m_ImageFileName;
}


void
DataModel
::SetNumberOfThreads(int threads) {
  itk::MultiThreader::SetGlobalDefaultNumberOfThreads(threads);
  itk::MultiThreader::SetGlobalMaximumNumberOfThreads(threads);
}


int
DataModel
::GetNumberOfThreads() {
  return itk::MultiThreader::GetGlobalMaximumNumberOfThreads();
}


void
DataModel
::SetMeasuredImageData(TImage::Pointer image) {
  m_MeasuredImageData = image;

  // Set image data.
  m_MeasuredImageITKToVTKFilter->SetInput(m_MeasuredImageData);
}


DataModel::TImage::Pointer
DataModel
::GetMeasuredImageData() {
  return m_MeasuredImageData;
}


vtkAlgorithmOutput*
DataModel
::GetMeasuredImageOutputPort() {
  return m_MeasuredImageITKToVTKFilter->GetOutputPort();
}


vtkAlgorithmOutput*
DataModel
::GetPSFImageOutputPort() {
  return m_PSFImageITKToVTKFilter->GetOutputPort();
}


vtkAlgorithmOutput*
DataModel
::GetBSFImageOutputPort() {
  return m_BSFImageITKToVTKFilter->GetOutputPort();
}


vtkAlgorithmOutput*
DataModel
::GetBSFDifferenceImageOutputPort() {
  return m_BSFDifferenceImageITKToVTKFilter->GetOutputPort();
}


double
DataModel
::GetMeasuredImageDataMinimum() {
  if (!GetMeasuredImageData())
    return 0.0;

  return m_MeasuredImageMinMaxFilter->GetMinimum();
}


double
DataModel
::GetMeasuredImageDataMaximum() {
  if (!GetMeasuredImageData())
    return 0.0;

  return m_MeasuredImageMinMaxFilter->GetMaximum();
}


DataModel::Float3DPointType
DataModel
::GetMeasuredImageDataMaximumCoordinates() {
  Float3DPointType point;
  Float3DImageType::Pointer image = GetMeasuredImageData();
  if (image) {
    image->TransformIndexToPhysicalPoint(m_MeasuredImageMinMaxFilter->GetIndexOfMaximum(), point);
  }

  return point;
}


int
DataModel
::GetNumberOfProperties() {
  return m_BeadSpreadFunctionSource->GetNumberOfParameters();
}


void
DataModel
::GetMeasuredImageDimensions(int dimensions[3]) {
  if (!GetMeasuredImageData()) {
    for (int i = 0; i < 3; i++)
      dimensions[i] = 0;
    return;
  }

  Float3DImageType::RegionType region
      = GetMeasuredImageData()->GetLargestPossibleRegion();
  itk::Size<3> size = region.GetSize();

  for (int i = 0; i < 3; i++)
    dimensions[i] = size[i];
}


void
DataModel
::SetMeasuredImageVoxelSpacing(double spacing[3]) {
  m_MeasuredImageData->SetSpacing(spacing);
  m_MeasuredImageITKToVTKFilter->GetOutputPort()->GetProducer()->Modified();
}


void
DataModel
::SetMeasuredImageVoxelSpacing(int dimension, double spacing) {
  if (!m_MeasuredImageData)
    return;

  SpacingType currentSpacing = m_MeasuredImageData->GetSpacing();
  currentSpacing[dimension] = spacing;

  SetMeasuredImageVoxelSpacing(currentSpacing.GetDataPointer());

  m_MeasuredImageITKToVTKFilter->GetOutputPort()->GetProducer()->Modified();
}


void
DataModel
::GetMeasuredImageVoxelSpacing(double spacing[3]) {
  if (!GetMeasuredImageData()) {
    for (int i = 0; i < 3; i++)
      spacing[i] = 0;
    return;
  }

  SpacingType thisSpacing = GetMeasuredImageData()->GetSpacing();
  for (int i = 0; i < 3; i++)
    spacing[i] = thisSpacing[i];
}


void
DataModel
::SetMeasuredImageOrigin(double origin[3]) {
  m_MeasuredImageData->SetOrigin(origin);
}


void
DataModel
::GetMeasuredImageOrigin(double origin[3]) {
  for (int i = 0; i < 3; i++) {
    origin[i] = m_MeasuredImageData->GetOrigin()[i];
  }
}


double
DataModel
::GetPSFImageDataMinimum() {
  if (!GetMeasuredImageData()) {
    return 0.0;
  }

  m_PointSpreadFunctionSource->UpdateLargestPossibleRegion();

  m_PSFImageMinMaxFilter->Compute();
  return m_PSFImageMinMaxFilter->GetMinimum();
}


double
DataModel
::GetPSFImageDataMaximum() {
  if (!GetMeasuredImageData()) {
    return 0.0;
  }

  m_PointSpreadFunctionSource->UpdateLargestPossibleRegion();

  m_PSFImageMinMaxFilter->Compute();
  return m_PSFImageMinMaxFilter->GetMaximum();
}


double
DataModel
::GetBSFImageDataMinimum() {
  if (!GetMeasuredImageData()) {
    return 0.0;
  }

  m_BeadSpreadFunctionSource->UpdateLargestPossibleRegion();

  m_BSFImageMinMaxFilter->Compute();
  return m_BSFImageMinMaxFilter->GetMinimum();
}


double
DataModel
::GetBSFImageDataMaximum() {
  if (!GetMeasuredImageData()) {
    return 0.0;
  }

  m_BeadSpreadFunctionSource->UpdateLargestPossibleRegion();

  m_BSFImageMinMaxFilter->Compute();
  return m_BSFImageMinMaxFilter->GetMaximum();
}


double
DataModel
::GetBSFDifferenceImageDataMinimum() {
  if (!GetMeasuredImageData()) {
    return 0.0;
  }

  m_BSFDifferenceImageFilter->Modified();
  m_BSFDifferenceImageFilter->UpdateLargestPossibleRegion();

  m_BSFDifferenceImageMinMaxFilter->Compute();
  return m_BSFDifferenceImageMinMaxFilter->GetMinimum();
}


double
DataModel
::GetBSFDifferenceImageDataMaximum() {
  if (!GetMeasuredImageData()) {
    return 0.0;
  }

  m_BSFDifferenceImageFilter->Modified();
  m_BSFDifferenceImageFilter->UpdateLargestPossibleRegion();

  m_BSFDifferenceImageMinMaxFilter->Compute();
  return m_BSFDifferenceImageMinMaxFilter->GetMaximum();
}


void
DataModel
::SetPSFImageDimensions(int dimensions[3]) {
  SizeType size;
  for (int i = 0; i < 3; i++)
    size[i] = static_cast<SizeValueType>(dimensions[i]);

  m_PointSpreadFunctionSource->SetSize(size);
}


void
DataModel
::SetPSFImageDimension(int index, int dimension) {
  int dimensions[3];
  GetPSFImageDimensions(dimensions);
  if (index >= 0 && index <= 2) {
    dimensions[index] = dimension;
    SetPSFImageDimensions(dimensions);
  }
}


void
DataModel
::GetPSFImageDimensions(int dimensions[3]) {
  if (GetMeasuredImageData()) {
    Float3DImageType::RegionType region
      = GetMeasuredImageData()->GetLargestPossibleRegion();
    itk::Size<3> size = region.GetSize();

    for (int i = 0; i < 3; i++)
      dimensions[i] = size[i];
  } else {
    for (int i = 0; i < 3; i++)
      dimensions[i] = 0;
  }
}


void
DataModel
::SetBSFImageDimensions(int dimensions[3]) {
  SizeType size;
  for (int i = 0; i < 3; i++)
    size[i] = static_cast<SizeValueType>(dimensions[i]);

  m_BeadSpreadFunctionSource->SetSize(size);
}


void
DataModel
::SetBSFImageDimension(int index, int dimension) {
  int dimensions[3];
  GetBSFImageDimensions(dimensions);
  if (index >= 0 && index <= 2) {
    dimensions[index] = dimension;
    SetBSFImageDimensions(dimensions);
  }
}


void
DataModel
::GetBSFImageDimensions(int dimensions[3]) {
  if (GetMeasuredImageData()) {
    Float3DImageType::RegionType region
      = GetMeasuredImageData()->GetLargestPossibleRegion();
    itk::Size<3> size = region.GetSize();

    for (int i = 0; i < 3; i++)
      dimensions[i] = size[i];

  } else {
    for (int i = 0; i < 3; i++)
      dimensions[i] = 0;
  }
}


void
DataModel
::SetPSFImageVoxelSpacing(double spacing[3]) {
  SpacingType thisSpacing;
  for (int i = 0; i < 3; i++)
    thisSpacing[i] = static_cast<SpacingValueType>(spacing[i]);

  m_PointSpreadFunctionSource->SetSpacing(thisSpacing);
  m_PSFImageITKToVTKFilter->GetOutputPort()->GetProducer()->Modified();
}


void
DataModel
::SetPSFImageVoxelSpacing(int dimension, double spacing) {
  if (!GetMeasuredImageData())
    return;

  TImage::SpacingType currentSpacing = m_MeasuredImageData->GetSpacing();
  currentSpacing[dimension] = spacing;

  SetPSFImageVoxelSpacing(currentSpacing.GetDataPointer());
}


void
DataModel
::GetPSFImageVoxelSpacing(double spacing[3]) {
  if (!GetMeasuredImageData()) {
    for (int i = 0; i < 3; i++)
      spacing[i] = 0.0;
    return;
  }

  SpacingType thisSpacing = GetMeasuredImageData()->GetSpacing();
  for (int i = 0; i < 3; i++)
    spacing[i] = thisSpacing[i];
}


void
DataModel
::SetBSFImageVoxelSpacing(double spacing[3]) {
  SpacingType thisSpacing;
  for (int i = 0; i < 3; i++)
    thisSpacing[i] = static_cast<SpacingValueType>(spacing[i]);

  m_BeadSpreadFunctionSource->SetSpacing(thisSpacing);
  m_BSFImageITKToVTKFilter->GetOutputPort()->GetProducer()->Modified();
}


void
DataModel
::SetBSFImageVoxelSpacing(int dimension, double spacing) {
  if (!GetMeasuredImageData())
    return;

  SpacingType currentSpacing = m_MeasuredImageData->GetSpacing();
  currentSpacing[dimension] = spacing;

  SetBSFImageVoxelSpacing(currentSpacing.GetDataPointer());
}


void
DataModel
::GetBSFImageVoxelSpacing(double spacing[3]) {
  if (!GetMeasuredImageData()) {
    for (int i = 0; i < 3; i++)
      spacing[i] = 0.0;
    return;
  }

  SpacingType thisSpacing = m_BeadSpreadFunctionSource->GetSpacing();
  for (int i = 0; i < 3; i++)
    spacing[i] = thisSpacing[i];
}


void
DataModel
::SetPSFImageOrigin(double origin[3]) {
  PointType thisOrigin;
  for (int i = 0; i < 3; i++)
    thisOrigin[i] = static_cast<PointValueType>(origin[i]);
  m_PointSpreadFunctionSource->SetOrigin(thisOrigin);
  m_PointSpreadFunctionSource->Modified();
}


void
DataModel
::GetPSFImageOrigin(double origin[3]) {
  PointType thisOrigin = m_PointSpreadFunctionSource->GetOrigin();
  for (int i = 0; i < 3; i++)
    origin[i] = static_cast<double>(thisOrigin[i]);
}


void
DataModel
::SetBSFImageOrigin(double origin[3]) {
  m_BeadSpreadFunctionSource->SetOrigin(origin);
  m_BeadSpreadFunctionSource->Modified();
}


void
DataModel
::GetBSFImageOrigin(double origin[3]) {
  PointType thisOrigin = m_BeadSpreadFunctionSource->GetOrigin();
  for (int i = 0; i < 3; i++)
    origin[i] = static_cast<double>(thisOrigin[i]);
}


void
DataModel
::RecenterImageOrigin() {
  double spacing[3];
  GetPSFImageVoxelSpacing(spacing);

  int size[3];
  GetPSFImageDimensions(size);

  PointType origin;
  for (int i = 0; i < 3; i++)
    origin[i] = -spacing[i]*static_cast<double>(size[i]-1)*0.5;
  m_BeadSpreadFunctionSource->SetOrigin(origin);
  m_PointSpreadFunctionSource->SetOrigin(origin);
  m_MeasuredImageData->SetOrigin(origin);
}


void
DataModel
::SetPSFPointCenter(double center[3]) {
  //PointType thisCenter;
  typedef ParametricImageSourceType::TransformType::OutputVectorType CenterType;
  CenterType thisCenter;
  for (int i = 0; i < 3; i++)
    thisCenter[i] = static_cast<PointValueType>(center[i]);
  m_PointSpreadFunctionSource->GetTransform()->SetTranslation(thisCenter);
}


void
DataModel
::GetPSFPointCenter(double center[3]) {
  //PointType thisCenter = m_PointSpreadFunctionSource->GetPointCenter();
  typedef ParametricImageSourceType::TransformType::OutputVectorType CenterType;
  CenterType thisCenter = m_PointSpreadFunctionSource->GetTransform()->GetTranslation();
  for (int i = 0; i < 3; i++)
    center[i] = static_cast<double>(thisCenter[i]);
}


void
DataModel
::SetBSFPointCenter(double center[3]) {
  PointType thisCenter;
  for (int i = 0; i < 3; i++)
    thisCenter[i] = static_cast<PointValueType>(center[i]);
  m_BeadSpreadFunctionSource->SetBeadCenter(thisCenter);
}


void
DataModel
::GetBSFPointCenter(double center[3]) {
  PointType thisCenter = m_BeadSpreadFunctionSource->GetBeadCenter();
  for (int i = 0; i < 3; i++)
    center[i] = static_cast<double>(thisCenter[i]);
}


void
DataModel
::UpdatePSFImage() {
  m_PointSpreadFunctionSource->UpdateLargestPossibleRegion();
  m_PSFImageMinMaxFilter->Compute();
}

void
DataModel
::UpdateBSFImage() {
  m_BeadSpreadFunctionSource->UpdateLargestPossibleRegion();
  m_BSFImageMinMaxFilter->Compute();
}


void
DataModel
::UpdateBSFDifferenceImage() {
  m_BSFDifferenceImageFilter->UpdateLargestPossibleRegion();
  m_BSFDifferenceImageMinMaxFilter->Compute();
}


void
DataModel
::SetBeadRadius(double radius) {
  m_BeadSpreadFunctionSource->SetBeadRadius(radius);
}


double
DataModel
::GetBeadRadius() {
  return m_BeadSpreadFunctionSource->GetBeadRadius();
}


void
DataModel
::SetShearX(double shear) {
  m_BeadSpreadFunctionSource->SetShearX(shear);
}


double
DataModel
::GetShearX() {
  return m_BeadSpreadFunctionSource->GetShearX();
}


void
DataModel
::SetShearY(double shear) {
  m_BeadSpreadFunctionSource->SetShearY(shear);
}


double
DataModel
::GetShearY() {
  return m_BeadSpreadFunctionSource->GetShearY();
}


void
DataModel
::SetIntensityShift(double intensity) {
  m_BeadSpreadFunctionSource->SetParameter(10, intensity);
}


double
DataModel
::GetIntensityShift() {
  return m_BeadSpreadFunctionSource->GetParameter(10);
}


void
DataModel
::SetIntensityScale(double scale) {
  m_BeadSpreadFunctionSource->SetParameter(9, scale);
}


double
DataModel
::GetIntensityScale() {
  return m_BeadSpreadFunctionSource->GetParameter(9);
}


std::string
DataModel
::GetParameterName(unsigned int index) const {
  unsigned int numBSFParameters = m_BeadSpreadFunctionSource->
    GetNumberOfBeadSpreadFunctionParameters();
  if (index < numBSFParameters) {
    return m_BSFParameterNames[index];
  } else {

    switch (m_PointSpreadFunctionType) {
    case GAUSSIAN_PSF:
      return m_GaussianPSFParameterNames[index - numBSFParameters];
      break;

    case GIBSON_LANNI_PSF:
    case HAEBERLE_PSF:
      return m_OPDBasedPSFParameterNames[index - numBSFParameters];
      break;

    case MODIFIED_GIBSON_LANNI_PSF:
      return m_ModifiedGibsonLanniPSFParameterNames[index - numBSFParameters];
      break;

    default:
      return std::string("unknown");
      break;
    }
  }

}

void
DataModel
::SetParameterValue(unsigned int index, double value) {
  m_BeadSpreadFunctionSource->SetParameter(index, value);

  unsigned int numBSFParameters = m_BeadSpreadFunctionSource->
    GetNumberOfBeadSpreadFunctionParameters();

  if (index < 3) {
    double spacing[3];
    GetBSFImageVoxelSpacing(spacing);
    SetPSFImageVoxelSpacing(spacing);
    SetMeasuredImageVoxelSpacing(spacing);

    RecenterImageOrigin();

  } else if (index >= numBSFParameters) {
    m_PointSpreadFunctionSource->SetParameter(index - numBSFParameters, value);
  }
}


double
DataModel
::GetParameterValue(unsigned int index) {
  return m_BeadSpreadFunctionSource->GetParameter(index);
}


std::string
DataModel
::GetParameterUnit(unsigned int index) const {
  unsigned int numBSFParameters = m_BeadSpreadFunctionSource->
    GetNumberOfBeadSpreadFunctionParameters();
  if (index < numBSFParameters) {
    return m_BSFParameterUnits[index];
  } else {

    switch (m_PointSpreadFunctionType) {
    case GAUSSIAN_PSF:
      return m_GaussianPSFParameterUnits[index - numBSFParameters];
      break;

    case GIBSON_LANNI_PSF:
    case HAEBERLE_PSF:
      return m_OPDBasedPSFParameterUnits[index - numBSFParameters];
      break;

    case MODIFIED_GIBSON_LANNI_PSF:
      return m_ModifiedGibsonLanniPSFParameterUnits[index - numBSFParameters];
      break;

    default:
      return std::string("unknown");
      break;
    }

  }
}


void
DataModel
::SetParameterEnabled(unsigned int index, bool enabled) {
#if 0
  try {
    ParametersMaskType* parametersMask = m_CostFunction->GetParametersMask();
    if (index < parametersMask->Size()) {
      parametersMask->SetElement(index, enabled ? 1 : 0);
    }
  } catch (...) {}
#endif

  if (index < m_BSFParameterMask.size()) {
    m_BSFParameterMask[index] = enabled;
  } else {

    index -= m_BSFParameterMask.size();

    switch (m_PointSpreadFunctionType) {
    case GAUSSIAN_PSF:
      m_GaussianPSFParameterMask[index] = enabled;
      break;

    case GIBSON_LANNI_PSF:
      m_GibsonLanniPSFParameterMask[index] = enabled;
      break;

    case MODIFIED_GIBSON_LANNI_PSF:
      m_ModifiedGibsonLanniPSFParameterMask[index] = enabled;
      break;

    default:
      break;
    }


  }
}


bool
DataModel
::GetParameterEnabled(unsigned int index) {
#if 0
  bool enabled = false;
  try {
    ParametersMaskType* parametersMask = m_CostFunction->GetParametersMask();
    if (index < parametersMask->Size()) {
      enabled = parametersMask->GetElement(index) != 0;
    }
  } catch (...) {}

  return enabled;
#endif

  if (index < m_BSFParameterMask.size()) {
    return m_BSFParameterMask[index];
  } else {

    index -= m_BSFParameterMask.size();

    switch (m_PointSpreadFunctionType) {
    case GAUSSIAN_PSF:
      return m_GaussianPSFParameterMask[index];
      break;

    case GIBSON_LANNI_PSF:
      return m_GibsonLanniPSFParameterMask[index];
      break;

    case MODIFIED_GIBSON_LANNI_PSF:
      return m_ModifiedGibsonLanniPSFParameterMask[index];
      break;

    default:
      return false;
      break;
    }

  }

}


void
DataModel
::SetZCoordinate(unsigned int index, double coordinate) {
  m_BeadSpreadFunctionSource->SetZCoordinate(index, coordinate);
}

double
DataModel
::GetZCoordinate(unsigned int index) {
  return m_BeadSpreadFunctionSource->GetZCoordinate(index);
}



void
DataModel
::SetUseCustomZCoordinates(bool use) {
  m_BeadSpreadFunctionSource->SetUseCustomZCoordinates(use);
}


bool
DataModel
::GetUseCustomZCoordinates() {
  return m_BeadSpreadFunctionSource->GetUseCustomZCoordinates();
}


void
DataModel
::UpdateMetricParameterMask() {
  ParametersMaskType* mask = m_CostFunction->GetParametersMask();

  int index = 0;
  typedef std::vector<bool>::size_type SizeType;

  SizeType i;
  for (i = 0; i < m_BSFParameterMask.size(); i++) {
    mask->SetElement(index++, m_BSFParameterMask[i] ? 1 : 0);
  }

  switch (m_PointSpreadFunctionType) {

  case GAUSSIAN_PSF:
    for (i = 0; i < m_GaussianPSFParameterMask.size(); i++) {
      mask->SetElement(index++, m_GaussianPSFParameterMask[i] ? 1 : 0);
    }
    break;

  case GIBSON_LANNI_PSF:
    for (i = 0; i < m_GibsonLanniPSFParameterMask.size(); i++) {
      mask->SetElement(index++, m_GibsonLanniPSFParameterMask[i] ? 1 : 0);
    }
    break;

  case MODIFIED_GIBSON_LANNI_PSF:
    for (i = 0; i < m_ModifiedGibsonLanniPSFParameterMask.size(); i++) {
      mask->SetElement(index++, m_ModifiedGibsonLanniPSFParameterMask[i] ? 1 : 0);
    }
    break;

  default:
  break;

  }
}


double
DataModel
::GetImageComparisonMetricValue() {
  UpdateMetricParameterMask();

  // Pass only the active parameter values to the cost function
  try {
    ParametersMaskType* mask = m_CostFunction->GetParametersMask();
    ParametersType activeParameters =
      ParametersType(m_CostFunction->GetNumberOfParameters());

    int activeIndex = 0;
    for (unsigned int i = 0; i < mask->Size(); i++) {
      if (mask->GetElement(i)) {
        activeParameters[activeIndex++] =
          m_BeadSpreadFunctionSource->GetParameters()[i];
      }
    }

    return m_CostFunction->GetValue(activeParameters);
  } catch (...) {}

  return 0.0;
}


void
DataModel
::Optimize() {
  UpdateMetricParameterMask();

  ParametersMaskType* mask = m_CostFunction->GetParametersMask();

  // Pluck out the active parameters
  ParametersType activeParameters(m_CostFunction->GetNumberOfParameters());
  ParametersType parameterScales(m_CostFunction->GetNumberOfParameters());
  int activeIndex = 0;
  for (unsigned int i = 0; i < mask->Size(); i++) {
    if (mask->GetElement(i)) {
      activeParameters[activeIndex] = m_BeadSpreadFunctionSource->GetParameters()[i];
      parameterScales[activeIndex++] = m_ParameterScales[i];
    }
  }

  // Connect to the cost function, set the initial parameters, and optimize.
  m_ImageToImageCostFunction
    ->SetFixedImageRegion(m_BeadSpreadFunctionSource->GetOutput()->GetLargestPossibleRegion());

  m_Optimizer = OptimizerType::New();
  m_Optimizer->AutomaticInitialSimplexOff();
  m_Optimizer->SetCostFunction(m_CostFunction);
  m_Optimizer->SetFunctionConvergenceTolerance(1e-1);
  m_Optimizer->SetInitialPosition(activeParameters);
  m_Optimizer->SetInitialSimplexDelta(parameterScales);

  m_Optimizer->StartOptimization();

  // Write the parameters back to the source object
  ParametersType optimizedParameters = m_Optimizer->GetCurrentPosition();
  ParametersType allParameters = m_BeadSpreadFunctionSource->GetParameters();
  activeIndex = 0;
  for (unsigned int i = 0; i < mask->Size(); i++) {
    if (mask->GetElement(i)) {
      allParameters[i] = optimizedParameters[activeIndex++];
    }
  }

  m_BeadSpreadFunctionSource->SetParameters(allParameters);

  // Update the PSF source used for display
  unsigned int numBSFParameters =
    m_BeadSpreadFunctionSource->GetNumberOfBeadSpreadFunctionParameters();
  unsigned int numPSFParameters = m_PointSpreadFunctionSource->GetNumberOfParameters();

  ParametersType psfParameters(numPSFParameters);
  for (unsigned int i = 0; i < numPSFParameters; i++) {
    psfParameters[i] = allParameters[i + numBSFParameters];
  }

  m_PointSpreadFunctionSource->SetParameters(psfParameters);
}

#endif // _DATA_MODEL_CXX_
