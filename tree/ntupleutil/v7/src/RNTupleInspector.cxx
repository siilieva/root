/// \file RNTupleInspector.cxx
/// \ingroup NTuple ROOT7
/// \author Florine de Geus <florine.willemijn.de.geus@cern.ch>
/// \date 2023-01-09
/// \warning This is part of the ROOT 7 prototype! It will change without notice. It might trigger earthquakes. Feedback
/// is welcome!

/*************************************************************************
 * Copyright (C) 1995-2023, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#include <ROOT/RColumnElement.hxx>
#include <ROOT/RError.hxx>
#include <ROOT/RPageStorageFile.hxx>
#include <ROOT/RNTupleDescriptor.hxx>
#include <ROOT/RNTupleInspector.hxx>
#include <ROOT/RError.hxx>

#include <TFile.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <exception>
#include <iomanip>
#include <iostream>

ROOT::Experimental::RNTupleInspector::RNTupleInspector(
   std::unique_ptr<ROOT::Experimental::Internal::RPageSource> pageSource)
   : fPageSource(std::move(pageSource))
{
   fPageSource->Attach();
   auto descriptorGuard = fPageSource->GetSharedDescriptorGuard();
   fDescriptor = descriptorGuard->Clone();

   CollectColumnInfo();
   CollectFieldTreeInfo(fDescriptor->GetFieldZeroId());
}

void ROOT::Experimental::RNTupleInspector::CollectColumnInfo()
{
   fCompressedSize = 0;
   fUncompressedSize = 0;

   for (const auto &colDesc : fDescriptor->GetColumnIterable()) {
      if (colDesc.IsAliasColumn())
         continue;

      auto colId = colDesc.GetPhysicalId();

      // We generate the default memory representation for the given column type in order
      // to report the size _in memory_ of column elements.
      auto colType = colDesc.GetModel().GetType();
      std::uint32_t elemSize = ROOT::Experimental::Internal::RColumnElementBase::Generate(colType)->GetSize();
      std::uint64_t nElems = 0;
      std::vector<std::uint64_t> compressedPageSizes{};

      for (const auto &clusterDescriptor : fDescriptor->GetClusterIterable()) {
         if (!clusterDescriptor.ContainsColumn(colId)) {
            continue;
         }

         auto columnRange = clusterDescriptor.GetColumnRange(colId);
         nElems += columnRange.fNElements;

         if (fCompressionSettings == -1) {
            fCompressionSettings = columnRange.fCompressionSettings;
         } else if (fCompressionSettings != columnRange.fCompressionSettings &&
                    columnRange.fCompressionSettings != kUnknownCompressionSettings) {
            // Note that currently all clusters and columns are compressed with the same settings and it is not yet
            // possible to do otherwise. This means that currently, this exception should never be thrown, but this
            // could change in the future.
            throw RException(R__FAIL("compression setting mismatch between column ranges (" +
                                     std::to_string(fCompressionSettings) + " vs " +
                                     std::to_string(columnRange.fCompressionSettings) + ")"));
         }

         const auto &pageRange = clusterDescriptor.GetPageRange(colId);

         for (const auto &page : pageRange.fPageInfos) {
            compressedPageSizes.emplace_back(page.fLocator.fBytesOnStorage);
            fUncompressedSize += page.fNElements * elemSize;
         }
      }

      fCompressedSize += std::accumulate(compressedPageSizes.begin(), compressedPageSizes.end(), 0);
      fColumnInfo.emplace(colId, RColumnInspector(colDesc, compressedPageSizes, elemSize, nElems));
   }
}

ROOT::Experimental::RNTupleInspector::RFieldTreeInspector
ROOT::Experimental::RNTupleInspector::CollectFieldTreeInfo(DescriptorId_t fieldId)
{
   std::uint64_t compressedSize = 0;
   std::uint64_t uncompressedSize = 0;

   for (const auto &colDescriptor : fDescriptor->GetColumnIterable(fieldId)) {
      auto colInfo = GetColumnInspector(colDescriptor.GetPhysicalId());
      compressedSize += colInfo.GetCompressedSize();
      uncompressedSize += colInfo.GetUncompressedSize();
   }

   for (const auto &subFieldDescriptor : fDescriptor->GetFieldIterable(fieldId)) {
      DescriptorId_t subFieldId = subFieldDescriptor.GetId();

      auto subFieldInfo = CollectFieldTreeInfo(subFieldId);

      compressedSize += subFieldInfo.GetCompressedSize();
      uncompressedSize += subFieldInfo.GetUncompressedSize();
   }

   auto fieldInfo = RFieldTreeInspector(fDescriptor->GetFieldDescriptor(fieldId), compressedSize, uncompressedSize);
   fFieldTreeInfo.emplace(fieldId, fieldInfo);
   return fieldInfo;
}

std::vector<ROOT::Experimental::DescriptorId_t>
ROOT::Experimental::RNTupleInspector::GetColumnsByFieldId(DescriptorId_t fieldId) const
{
   std::vector<DescriptorId_t> colIds;
   std::deque<DescriptorId_t> fieldIdQueue{fieldId};

   while (!fieldIdQueue.empty()) {
      auto currId = fieldIdQueue.front();
      fieldIdQueue.pop_front();

      for (const auto &col : fDescriptor->GetColumnIterable(currId)) {
         if (col.IsAliasColumn()) {
            continue;
         }

         colIds.emplace_back(col.GetPhysicalId());
      }

      for (const auto &fld : fDescriptor->GetFieldIterable(currId)) {
         fieldIdQueue.push_back(fld.GetId());
      }
   }

   return colIds;
}

std::unique_ptr<ROOT::Experimental::RNTupleInspector>
ROOT::Experimental::RNTupleInspector::Create(ROOT::Experimental::RNTuple *sourceNTuple)
{
   if (!sourceNTuple) {
      throw RException(R__FAIL("provided RNTuple is null"));
   }

   auto pageSource = Internal::RPageSourceFile::CreateFromAnchor(*sourceNTuple);
   return std::unique_ptr<RNTupleInspector>(new RNTupleInspector(std::move(pageSource)));
}

std::unique_ptr<ROOT::Experimental::RNTupleInspector>
ROOT::Experimental::RNTupleInspector::Create(std::string_view ntupleName, std::string_view sourceFileName)
{
   auto pageSource = ROOT::Experimental::Internal::RPageSource::Create(ntupleName, sourceFileName);
   return std::unique_ptr<RNTupleInspector>(new RNTupleInspector(std::move(pageSource)));
}

std::string ROOT::Experimental::RNTupleInspector::GetCompressionSettingsAsString() const
{
   int algorithm = fCompressionSettings / 100;
   int level = fCompressionSettings - (algorithm * 100);

   return RCompressionSetting::AlgorithmToString(static_cast<RCompressionSetting::EAlgorithm::EValues>(algorithm)) +
          " (level " + std::to_string(level) + ")";
}

//------------------------------------------------------------------------------

const ROOT::Experimental::RNTupleInspector::RColumnInspector &
ROOT::Experimental::RNTupleInspector::GetColumnInspector(DescriptorId_t physicalColumnId) const
{
   if (physicalColumnId > fDescriptor->GetNPhysicalColumns()) {
      throw RException(R__FAIL("No column with physical ID " + std::to_string(physicalColumnId) + " present"));
   }

   return fColumnInfo.at(physicalColumnId);
}

size_t ROOT::Experimental::RNTupleInspector::GetColumnCountByType(ROOT::Experimental::EColumnType colType) const
{
   size_t typeCount = 0;

   for (auto &[colId, colInfo] : fColumnInfo) {
      if (colInfo.GetType() == colType) {
         ++typeCount;
      }
   }

   return typeCount;
}

const std::vector<ROOT::Experimental::DescriptorId_t>
ROOT::Experimental::RNTupleInspector::GetColumnsByType(ROOT::Experimental::EColumnType colType)
{
   std::vector<DescriptorId_t> colIds;

   for (const auto &[colId, colInfo] : fColumnInfo) {
      if (colInfo.GetType() == colType)
         colIds.emplace_back(colId);
   }

   return colIds;
}

const std::vector<ROOT::Experimental::EColumnType> ROOT::Experimental::RNTupleInspector::GetColumnTypes()
{
   std::set<EColumnType> colTypes;

   for (const auto &[colId, colInfo] : fColumnInfo) {
      colTypes.emplace(colInfo.GetType());
   }

   return std::vector(colTypes.begin(), colTypes.end());
}

void ROOT::Experimental::RNTupleInspector::PrintColumnTypeInfo(ENTupleInspectorPrintFormat format, std::ostream &output)
{
   struct ColumnTypeInfo {
      std::uint32_t count;
      std::uint64_t nElems, compressedSize, uncompressedSize;

      void operator+=(const RColumnInspector &colInfo)
      {
         this->count++;
         this->nElems += colInfo.GetNElements();
         this->compressedSize += colInfo.GetCompressedSize();
         this->uncompressedSize += colInfo.GetUncompressedSize();
      }
   };

   std::map<EColumnType, ColumnTypeInfo> colTypeInfo;

   for (const auto &[colId, colInfo] : fColumnInfo) {
      colTypeInfo[colInfo.GetType()] += colInfo;
   }

   switch (format) {
   case ENTupleInspectorPrintFormat::kTable:
      output << " column type    | count   | # elements      | compressed bytes  | uncompressed bytes\n"
             << "----------------|---------|-----------------|-------------------|--------------------" << std::endl;
      for (const auto &[colType, typeInfo] : colTypeInfo) {
         output << std::setw(15) << Internal::RColumnElementBase::GetTypeName(colType) << " |" << std::setw(8)
                << typeInfo.count << " |" << std::setw(16) << typeInfo.nElems << " |" << std::setw(18)
                << typeInfo.compressedSize << " |" << std::setw(18) << typeInfo.uncompressedSize << " " << std::endl;
      }
      break;
   case ENTupleInspectorPrintFormat::kCSV:
      output << "columnType,count,nElements,compressedSize,uncompressedSize" << std::endl;
      for (const auto &[colType, typeInfo] : colTypeInfo) {
         output << Internal::RColumnElementBase::GetTypeName(colType) << "," << typeInfo.count << "," << typeInfo.nElems
                << "," << typeInfo.compressedSize << "," << typeInfo.uncompressedSize << std::endl;
      }
      break;
   default: throw RException(R__FAIL("Invalid print format"));
   }
}

std::unique_ptr<TH1D>
ROOT::Experimental::RNTupleInspector::GetColumnTypeInfoAsHist(ROOT::Experimental::ENTupleInspectorHist histKind,
                                                              std::string_view histName, std::string_view histTitle)
{
   if (histName.empty()) {
      switch (histKind) {
      case ENTupleInspectorHist::kCount: histName = "colTypeCountHist"; break;
      case ENTupleInspectorHist::kNElems: histName = "colTypeElemCountHist"; break;
      case ENTupleInspectorHist::kCompressedSize: histName = "colTypeCompSizeHist"; break;
      case ENTupleInspectorHist::kUncompressedSize: histName = "colTypeUncompSizeHist"; break;
      default: throw RException(R__FAIL("Unknown histogram type"));
      }
   }

   if (histTitle.empty()) {
      switch (histKind) {
      case ENTupleInspectorHist::kCount: histTitle = "Column count by type"; break;
      case ENTupleInspectorHist::kNElems: histTitle = "Number of elements by column type"; break;
      case ENTupleInspectorHist::kCompressedSize: histTitle = "Compressed size by column type"; break;
      case ENTupleInspectorHist::kUncompressedSize: histTitle = "Uncompressed size by column type"; break;
      default: throw RException(R__FAIL("Unknown histogram type"));
      }
   }

   auto hist = std::make_unique<TH1D>(std::string(histName).c_str(), std::string(histTitle).c_str(), 1, 0, 1);

   double data;
   for (const auto &[colId, colInfo] : fColumnInfo) {
      switch (histKind) {
      case ENTupleInspectorHist::kCount: data = 1.; break;
      case ENTupleInspectorHist::kNElems: data = colInfo.GetNElements(); break;
      case ENTupleInspectorHist::kCompressedSize: data = colInfo.GetCompressedSize(); break;
      case ENTupleInspectorHist::kUncompressedSize: data = colInfo.GetUncompressedSize(); break;
      default: throw RException(R__FAIL("Unknown histogram type"));
      }

      hist->AddBinContent(
         hist->GetXaxis()->FindBin(Internal::RColumnElementBase::GetTypeName(colInfo.GetType()).c_str()), data);
   }

   return hist;
}

std::unique_ptr<TH1D> ROOT::Experimental::RNTupleInspector::GetPageSizeDistribution(DescriptorId_t physicalColumnId,
                                                                                    std::string histName,
                                                                                    std::string histTitle, size_t nBins)
{
   if (histTitle.empty())
      histTitle = "Page size distribution for column with ID " + std::to_string(physicalColumnId);

   return GetPageSizeDistribution({physicalColumnId}, histName, histTitle, nBins);
}

std::unique_ptr<TH1D>
ROOT::Experimental::RNTupleInspector::GetPageSizeDistribution(ROOT::Experimental::EColumnType colType,
                                                              std::string histName, std::string histTitle, size_t nBins)
{
   if (histName.empty())
      histName = "pageSizeHistCol" + Internal::RColumnElementBase::GetTypeName(colType);
   if (histTitle.empty())
      histTitle = "Page size distribution for columns with type " + Internal::RColumnElementBase::GetTypeName(colType);

   auto perTypeHist = GetPageSizeDistribution({colType}, histName, histTitle, nBins);

   if (perTypeHist->GetNhists() < 1)
      return std::make_unique<TH1D>(histName.c_str(), histTitle.c_str(), 64, 0, 0);

   auto hist = std::unique_ptr<TH1D>(dynamic_cast<TH1D *>(perTypeHist->GetHists()->First()));

   hist->SetName(histName.c_str());
   hist->SetTitle(histTitle.c_str());
   hist->SetXTitle("Page size (B)");
   hist->SetYTitle("N_{pages}");
   return hist;
}

std::unique_ptr<TH1D>
ROOT::Experimental::RNTupleInspector::GetPageSizeDistribution(std::initializer_list<DescriptorId_t> colIds,
                                                              std::string histName, std::string histTitle, size_t nBins)
{
   auto hist = std::make_unique<TH1D>();

   if (histName.empty())
      histName = "pageSizeHist";
   hist->SetName(histName.c_str());
   if (histTitle.empty())
      histTitle = "Page size distribution";
   hist->SetTitle(histTitle.c_str());
   hist->SetXTitle("Page size (B)");
   hist->SetYTitle("N_{pages}");

   std::vector<std::uint64_t> pageSizes;
   std::for_each(colIds.begin(), colIds.end(), [this, &pageSizes](const auto colId) {
      auto colInfo = GetColumnInspector(colId);
      pageSizes.insert(pageSizes.end(), colInfo.GetCompressedPageSizes().begin(),
                       colInfo.GetCompressedPageSizes().end());
   });

   auto histMinMax = std::minmax_element(pageSizes.begin(), pageSizes.end());
   hist->SetBins(nBins, *histMinMax.first,
                 *histMinMax.second + ((*histMinMax.second - *histMinMax.first) / static_cast<double>(nBins)));

   for (const auto pageSize : pageSizes) {
      hist->Fill(pageSize);
   }

   return hist;
}

std::unique_ptr<THStack> ROOT::Experimental::RNTupleInspector::GetPageSizeDistribution(
   std::initializer_list<ROOT::Experimental::EColumnType> colTypes, std::string histName, std::string histTitle,
   size_t nBins)
{
   if (histName.empty())
      histName = "pageSizeHist";
   if (histTitle.empty())
      histTitle = "Per-column type page size distribution";

   auto stackedHist = std::make_unique<THStack>(histName.c_str(), histTitle.c_str());

   double histMin = std::numeric_limits<double>::max();
   double histMax = 0;
   std::map<EColumnType, std::vector<std::uint64_t>> pageSizes;

   std::vector<EColumnType> colTypeVec = colTypes;
   if (std::empty(colTypes)) {
      colTypeVec = GetColumnTypes();
   }

   for (const auto colType : colTypeVec) {
      auto colIds = GetColumnsByType(colType);

      if (colIds.empty())
         continue;

      std::vector<std::uint64_t> pageSizesForColType;
      std::for_each(colIds.cbegin(), colIds.cend(), [this, &pageSizesForColType](const auto colId) {
         auto colInfo = GetColumnInspector(colId);
         pageSizesForColType.insert(pageSizesForColType.end(), colInfo.GetCompressedPageSizes().begin(),
                                    colInfo.GetCompressedPageSizes().end());
      });
      pageSizes.emplace(colType, pageSizesForColType);

      auto histMinMax = std::minmax_element(pageSizesForColType.begin(), pageSizesForColType.end());
      histMin = std::min(histMin, static_cast<double>(*histMinMax.first));
      histMax = std::max(histMax, static_cast<double>(*histMinMax.second));
   }

   for (const auto &[colType, pageSizesForColType] : pageSizes) {
      auto hist = std::make_unique<TH1D>(
         TString::Format("%s%s", histName.c_str(), Internal::RColumnElementBase::GetTypeName(colType).c_str()),
         Internal::RColumnElementBase::GetTypeName(colType).c_str(), nBins, histMin,
         histMax + ((histMax - histMin) / static_cast<double>(nBins)));

      for (const auto pageSize : pageSizesForColType) {
         hist->Fill(pageSize);
      }

      stackedHist->Add(hist.release());
   }

   return stackedHist;
}

//------------------------------------------------------------------------------

const ROOT::Experimental::RNTupleInspector::RFieldTreeInspector &
ROOT::Experimental::RNTupleInspector::GetFieldTreeInspector(DescriptorId_t fieldId) const
{
   if (fieldId >= fDescriptor->GetNFields()) {
      throw RException(R__FAIL("No field with ID " + std::to_string(fieldId) + " present"));
   }

   return fFieldTreeInfo.at(fieldId);
}

const ROOT::Experimental::RNTupleInspector::RFieldTreeInspector &
ROOT::Experimental::RNTupleInspector::GetFieldTreeInspector(std::string_view fieldName) const
{
   DescriptorId_t fieldId = fDescriptor->FindFieldId(fieldName);

   if (fieldId == kInvalidDescriptorId) {
      throw RException(R__FAIL("Could not find field `" + std::string(fieldName) + "`"));
   }

   return GetFieldTreeInspector(fieldId);
}

size_t ROOT::Experimental::RNTupleInspector::GetFieldCountByType(const std::regex &typeNamePattern,
                                                                 bool includeSubFields) const
{
   size_t typeCount = 0;

   for (auto &[fldId, fldInfo] : fFieldTreeInfo) {
      if (!includeSubFields && fldInfo.GetDescriptor().GetParentId() != fDescriptor->GetFieldZeroId()) {
         continue;
      }

      if (std::regex_match(fldInfo.GetDescriptor().GetTypeName(), typeNamePattern)) {
         typeCount++;
      }
   }

   return typeCount;
}

const std::vector<ROOT::Experimental::DescriptorId_t>
ROOT::Experimental::RNTupleInspector::GetFieldsByName(const std::regex &fieldNamePattern, bool searchInSubFields) const
{
   std::vector<DescriptorId_t> fieldIds;

   for (auto &[fldId, fldInfo] : fFieldTreeInfo) {

      if (!searchInSubFields && fldInfo.GetDescriptor().GetParentId() != fDescriptor->GetFieldZeroId()) {
         continue;
      }

      if (std::regex_match(fldInfo.GetDescriptor().GetFieldName(), fieldNamePattern)) {
         fieldIds.emplace_back(fldId);
      }
   }

   return fieldIds;
}
