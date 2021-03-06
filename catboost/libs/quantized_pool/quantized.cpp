#include "quantized.h"

#include <catboost/libs/column_description/column.h>

#include <util/generic/algorithm.h>

THashMap<size_t, size_t> GetColumnIndexToFlatIndexMap(const NCB::TQuantizedPool& pool) {
    TVector<size_t> columnIndices;
    columnIndices.reserve(pool.ColumnIndexToLocalIndex.size());
    for (const auto& [columnIndex, localIndex] : pool.ColumnIndexToLocalIndex) {
        const auto columnType = pool.ColumnTypes[localIndex];
        if (!IsFactorColumn(columnType)) {
            continue;
        }

        columnIndices.push_back(columnIndex);
    }

    Sort(columnIndices);

    THashMap<size_t, size_t> map;
    map.reserve(columnIndices.size());
    for (size_t i = 0; i < columnIndices.size(); ++i) {
        map.emplace(columnIndices[i], map.size());
    }

    return map;
}

TVector<TString> GetFlatFeatureNames(const NCB::TQuantizedPool& pool) {
    const auto columnIndexToFlatIndex = GetColumnIndexToFlatIndexMap(pool);
    TVector<TString> names(columnIndexToFlatIndex.size());
    for (const auto [columnIndex, flatIndex] : columnIndexToFlatIndex) {
        const auto localIndex = pool.ColumnIndexToLocalIndex.at(columnIndex);
        names[flatIndex] = pool.ColumnNames[localIndex];
    }
    return names;
}

THashMap<size_t, size_t> GetColumnIndexToNumericFeatureIndexMap(const NCB::TQuantizedPool& pool) {
    TVector<size_t> columnIndices;
    columnIndices.reserve(pool.ColumnIndexToLocalIndex.size());
    for (const auto [columnIndex, localIndex] : pool.ColumnIndexToLocalIndex) {
        const auto columnType = pool.ColumnTypes[localIndex];
        if (columnType != EColumn::Num) {
            continue;
        }

        columnIndices.push_back(columnIndex);
    }

    Sort(columnIndices);

    THashMap<size_t, size_t> map;
    map.reserve(columnIndices.size());
    for (size_t i = 0; i < columnIndices.size(); ++i) {
        map.emplace(columnIndices[i], map.size());
    }

    return map;
}

TPoolMetaInfo GetPoolMetaInfo(const NCB::TQuantizedPool& pool, bool hasAdditionalGroupWeight) {
    TPoolMetaInfo metaInfo;

    const size_t columnsCount = pool.ColumnIndexToLocalIndex.size();
    metaInfo.ColumnsInfo = TPoolColumnsMetaInfo();
    metaInfo.ColumnsInfo->Columns.resize(columnsCount);

    for (const auto [columnIndex, localIndex] : pool.ColumnIndexToLocalIndex) {
        const auto columnType = pool.ColumnTypes[localIndex];
        metaInfo.FeatureCount += static_cast<ui32>(IsFactorColumn(columnType));
        metaInfo.BaselineCount += static_cast<ui32>(columnType == EColumn::Baseline);
        metaInfo.HasGroupId |= columnType == EColumn::GroupId;
        metaInfo.HasGroupWeight |= (columnType == EColumn::GroupWeight) || hasAdditionalGroupWeight;
        metaInfo.HasSubgroupIds |= columnType == EColumn::SubgroupId;
        metaInfo.HasWeights |= columnType == EColumn::Weight;
        metaInfo.HasTimestamp |= columnType == EColumn::Timestamp;
        metaInfo.ColumnsInfo->Columns[columnIndex].Type = columnType;
        metaInfo.ColumnsInfo->Columns[columnIndex].Id = pool.ColumnNames[localIndex];
    }

    metaInfo.Validate();
    return metaInfo;
}

TVector<int> GetCategoricalFeatureIndices(const NCB::TQuantizedPool& pool) {
    const auto columnIndexToFeatureIndex = GetColumnIndexToFlatIndexMap(pool);

    TVector<int> categoricalIds;
    for (const auto [columnIndex, localIndex] : pool.ColumnIndexToLocalIndex) {
        const auto columnType = pool.ColumnTypes[localIndex];
        if (columnType != EColumn::Categ) {
            continue;
        }

        const auto featureIndex = columnIndexToFeatureIndex.at(columnIndex);
        categoricalIds.push_back(static_cast<int>(featureIndex));
    }

    Sort(categoricalIds);

    return categoricalIds;
}

TVector<int> GetIgnoredFlatIndices(const NCB::TQuantizedPool& pool) {
    const auto columnIndexToFeatureIndex = GetColumnIndexToFlatIndexMap(pool);
    TVector<int> indices;
    for (const auto [columnIndex, localIndex] : pool.ColumnIndexToLocalIndex) {
        const auto columnType = pool.ColumnTypes[localIndex];
        if (columnType != EColumn::Num && columnType != EColumn::Categ) {
            continue;
        }

        const auto featureIndex = columnIndexToFeatureIndex.at(columnIndex);
        if (IsIn(pool.IgnoredColumnIndices, columnIndex)) {
            indices.push_back(static_cast<int>(featureIndex));
            continue;
        }

        const auto it = pool.QuantizationSchema.GetFeatureIndexToSchema().find(featureIndex);
        if (it == pool.QuantizationSchema.GetFeatureIndexToSchema().end()) {
            // categorical features are not quantized right now
            indices.push_back(featureIndex);
            continue;
        } else if (it->second.GetBorders().empty()) {
            indices.push_back(featureIndex);
            continue;
        }
    }

    Sort(indices);

    return indices;
}
