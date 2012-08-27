/*
 *  KEABand.cpp
 *
 *  Created by Pete Bunting on 01/08/2012.
 *  Copyright 2012 LibKEA. All rights reserved.
 *
 *  This file is part of LibKEA.
 *
 *  Permission is hereby granted, free of charge, to any person 
 *  obtaining a copy of this software and associated documentation 
 *  files (the "Software"), to deal in the Software without restriction, 
 *  including without limitation the rights to use, copy, modify, 
 *  merge, publish, distribute, sublicense, and/or sell copies of the 
 *  Software, and to permit persons to whom the Software is furnished 
 *  to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be 
 *  included in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
 *  OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
 *  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR 
 *  ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF 
 *  CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION 
 *  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "KEABand.h"
#include "KEAOverview.h"

#include "gdal_rat.h"
#include "libkea/KEAAttributeTable.h"

#include <map>
#include <vector>

// constructor
KEARasterBand::KEARasterBand( KEADataset *pDataset, int nSrcBand, GDALAccess eAccess, libkea::KEAImageIO *pImageIO, int *pRefCount )
{
    this->poDS = pDataset; // our pointer onto the dataset
    this->nBand = nSrcBand; // this is the band we are
    this->m_eKEADataType = pImageIO->getImageBandDataType(nSrcBand); // get the data type as KEA enum
    this->eDataType = KEA_to_GDAL_Type( m_eKEADataType );       // convert to GDAL enum
    this->nBlockXSize = pImageIO->getImageBlockSize(nSrcBand);  // get the native blocksize
    this->nBlockYSize = pImageIO->getImageBlockSize(nSrcBand);
    this->nRasterXSize = this->poDS->GetRasterXSize();          // ask the dataset for the total image size
    this->nRasterYSize = this->poDS->GetRasterYSize();
    this->eAccess = eAccess;

    // grab the imageio class and its refcount
    this->m_pImageIO = pImageIO;
    this->m_pnRefCount = pRefCount;
    // increment the refcount as we now have a reference to imageio
    (*this->m_pnRefCount)++;

    // initialis overview variables
    m_nOverviews = 0;
    m_panOverviewBands = NULL;

    // grab the description here
    this->sDescription = pImageIO->getImageBandDescription(nSrcBand);

    this->m_pAttributeTable = NULL;  // no RAT yet
    this->m_pColorTable = NULL;     // no color table yet

    // initialise the metadata as a CPLStringList
    m_papszMetadataList = NULL;
    this->UpdateMetadataList();
}

// destructor
KEARasterBand::~KEARasterBand()
{
    // destroy RAT if any
    delete this->m_pAttributeTable;
    // destroy color table if any
    delete this->m_pColorTable;
    // destroy the metadata
    CSLDestroy(this->m_papszMetadataList);
    // delete any overview bands
    this->deleteOverviewObjects();

    // according to the docs, this is required
    this->FlushCache();

    // decrement the recount and delete if needed
    (*m_pnRefCount)--;
    if( *m_pnRefCount == 0 )
    {
        m_pImageIO->close();
        delete m_pImageIO;
        delete m_pnRefCount;
    }
}

// internal method that updates the metadata into m_papszMetadataList
void KEARasterBand::UpdateMetadataList()
{
    std::vector< std::pair<std::string, std::string> > data;

    // get all the metadata and iterate through
    data = this->m_pImageIO->getImageBandMetaData(this->nBand);
    for(std::vector< std::pair<std::string, std::string> >::iterator iterMetaData = data.begin(); iterMetaData != data.end(); ++iterMetaData)
    {
        // add to our list
        m_papszMetadataList = CSLSetNameValue(m_papszMetadataList, iterMetaData->first.c_str(), iterMetaData->second.c_str());
    }
    // we have a pseudo metadata item that tells if we are thematic 
    // or continuous like the HFA driver
    if( this->m_pImageIO->getImageBandLayerType(this->nBand) == libkea::kea_continuous )
    {
        m_papszMetadataList = CSLSetNameValue(m_papszMetadataList, "LAYER_TYPE", "athematic" );
    }
    else
    {
        m_papszMetadataList = CSLSetNameValue(m_papszMetadataList, "LAYER_TYPE", "thematic" );
    }
    // another one for the histogram
    std::string sHistogram = GetHistogramAsMetadata();
    if( !sHistogram.empty() )
    {
        m_papszMetadataList = CSLSetNameValue(m_papszMetadataList, "STATISTICS_HISTOBINVALUES", sHistogram.c_str() );
    }
}

// internal method to create the overviews
void KEARasterBand::CreateOverviews(int nOverviews, int *panOverviewList)
{
    // delete any existing overview bands
    this->deleteOverviewObjects();

    // allocate space
    m_panOverviewBands = (KEAOverview**)CPLMalloc(sizeof(KEAOverview*) * nOverviews);
    m_nOverviews = nOverviews;

    // loop through and create the overviews
    int nFactor, nXSize, nYSize;
    for( int nCount = 0; nCount < m_nOverviews; nCount++ )
    {
        nFactor = panOverviewList[nCount];
        // divide by the factor to get the new size
        nXSize = this->nRasterXSize / nFactor;
        nYSize = this->nRasterYSize / nFactor;

        // tell image io to create a new overview
        this->m_pImageIO->createOverview(this->nBand, nCount + 1, nXSize, nYSize);

        // create one of our objects to represent it
        m_panOverviewBands[nCount] = new KEAOverview((KEADataset*)this->poDS, this->nBand, GA_Update,
                                        this->m_pImageIO, this->m_pnRefCount, nCount + 1, nXSize, nYSize);
    }
}

// virtual method to read a block
CPLErr KEARasterBand::IReadBlock( int nBlockXOff, int nBlockYOff, void * pImage )
{
    try
    {
        // GDAL deals in blocks - if we are at the end of a row
        // we need to adjust the amount read so we don't go over the edge
        int xsize = this->nBlockXSize;
        int xtotalsize = this->nBlockXSize * (nBlockXOff + 1);
        if( xtotalsize > this->nRasterXSize )
        {
            xsize -= (xtotalsize - this->nRasterXSize);
        }
        int ysize = this->nBlockYSize;
        int ytotalsize = this->nBlockYSize * (nBlockYOff + 1);
        if( ytotalsize > this->nRasterYSize )
        {
            ysize -= (ytotalsize - this->nRasterYSize);
        }
        this->m_pImageIO->readImageBlock2Band( this->nBand, pImage, this->nBlockXSize * nBlockXOff,
                                            this->nBlockYSize * nBlockYOff,
                                            xsize, ysize, this->nBlockXSize, this->nBlockYSize, 
                                            this->m_eKEADataType );
        return CE_None;
    }
    catch (libkea::KEAIOException &e)
    {
        CPLError( CE_Failure, CPLE_AppDefined,
                "Failed to read file: %s", e.what() );
        return CE_Failure;
    }
}

// virtual method to write a block
CPLErr KEARasterBand::IWriteBlock( int nBlockXOff, int nBlockYOff, void * pImage )
{
    try
    {
        // GDAL deals in blocks - if we are at the end of a row
        // we need to adjust the amount written so we don't go over the edge
        int xsize = this->nBlockXSize;
        int xtotalsize = this->nBlockXSize * (nBlockXOff + 1);
        if( xtotalsize > this->nRasterXSize )
        {
            xsize -= (xtotalsize - this->nRasterXSize);
        }
        int ysize = this->nBlockYSize;
        int ytotalsize = this->nBlockYSize * (nBlockYOff + 1);
        if( ytotalsize > this->nRasterYSize )
        {
            ysize -= (ytotalsize - this->nRasterYSize);
        }

        this->m_pImageIO->writeImageBlock2Band( this->nBand, pImage, this->nBlockXSize * nBlockXOff,
                                            this->nBlockYSize * nBlockYOff,
                                            xsize, ysize, this->nBlockXSize, this->nBlockYSize,
                                            this->m_eKEADataType );
        return CE_None;
    }
    catch (libkea::KEAIOException &e)
    {
        CPLError( CE_Failure, CPLE_AppDefined,
                "Failed to write file: %s", e.what() );
        return CE_Failure;
    }
}

void KEARasterBand::SetDescription(const char *pszDescription)
{
    try
    {
        this->m_pImageIO->setImageBandDescription(this->nBand, pszDescription);
        this->sDescription = pszDescription;
    }
    catch (libkea::KEAIOException &e)
    {
        // ignore?
    }
}

const char *KEARasterBand::GetDescription() const
{
    // do we need to implement this?
    return this->sDescription.c_str();
}

// set a metadata item
CPLErr KEARasterBand::SetMetadataItem(const char *pszName, const char *pszValue, const char *pszDomain)
{
    // only deal with 'default' domain - no geolocation etc
    if( ( pszDomain != NULL ) && ( *pszDomain != '\0' ) )
        return CE_Failure;
    try
    {
        // if it is LAYER_TYPE handle it seperately
        if( EQUAL( pszName, "LAYER_TYPE" ) )
        {
            if( EQUAL( pszValue, "athematic" ) )
            {
                this->m_pImageIO->setImageBandLayerType(this->nBand, libkea::kea_continuous );
            }
            else
            {
                this->m_pImageIO->setImageBandLayerType(this->nBand, libkea::kea_thematic );
            }
        }
        // STATISTICS_HISTOBINVALUES handled separately also
        else if( EQUAL( pszName, "STATISTICS_HISTOBINVALUES" ) )
        {
            SetHistogramFromMetadata( pszValue );
        }
        else
        {
            // otherwise set it as normal
            this->m_pImageIO->setImageBandMetaData(this->nBand, pszName, pszValue );
        }
        // CSLSetNameValue will update if already there
        m_papszMetadataList = CSLSetNameValue( m_papszMetadataList, pszName, pszValue );
        return CE_None;
    }
    catch (libkea::KEAIOException &e)
    {
        return CE_Failure;
    }
}

// get a single metdata item
const char *KEARasterBand::GetMetadataItem (const char *pszName, const char *pszDomain)
{
    // only deal with 'default' domain - no geolocation etc
    if( ( pszDomain != NULL ) && ( *pszDomain != '\0' ) )
        return NULL;
    // get it out of the CSLStringList so we can be sure it is persistant
    return CSLFetchNameValue(m_papszMetadataList, pszName);
}

// get all the metadata as a CSLStringList
char **KEARasterBand::GetMetadata(const char *pszDomain)
{
    // only deal with 'default' domain - no geolocation etc
    if( ( pszDomain != NULL ) && ( *pszDomain != '\0' ) )
        return NULL;
    // conveniently we already have it in this format
    return m_papszMetadataList; 
}

// set the metdata as a CSLStringList
CPLErr KEARasterBand::SetMetadata(char **papszMetadata, const char *pszDomain)
{
    // only deal with 'default' domain - no geolocation etc
    if( ( pszDomain != NULL ) && ( *pszDomain != '\0' ) )
        return CE_Failure;
    int nIndex = 0;
    char *pszName;
    const char *pszValue;
    try
    {
        // iterate through each one
        while( papszMetadata[nIndex] != NULL )
        {
            pszValue = CPLParseNameValue( papszMetadata[nIndex], &pszName );

            // it is LAYER_TYPE? if so handle seperately
            if( EQUAL( pszName, "LAYER_TYPE" ) )
            {
                if( EQUAL( pszValue, "athematic" ) )
                {
                    this->m_pImageIO->setImageBandLayerType(this->nBand, libkea::kea_continuous );
                }
                else
                {
                    this->m_pImageIO->setImageBandLayerType(this->nBand, libkea::kea_thematic );
                }
            }
            // STATISTICS_HISTOBINVALUES handled separately also
            else if( EQUAL( pszName, "STATISTICS_HISTOBINVALUES" ) )
            {
                SetHistogramFromMetadata( pszValue );
            }
            else
            {
                // write it into the image
                this->m_pImageIO->setImageBandMetaData(this->nBand, pszName, pszValue );
            }
            nIndex++;
        }
    }
    catch (libkea::KEAIOException &e)
    {
        return CE_Failure;
    }
    // destroy our list and duplicate the one passed in
    // and use that as our list from now on
    CSLDestroy(m_papszMetadataList);
    m_papszMetadataList = CSLDuplicate(papszMetadata);
    return CE_None;
}

// get the no data value
double KEARasterBand::GetNoDataValue(int *pbSuccess)
{
    try
    {
        double dVal;
        this->m_pImageIO->getNoDataValue(this->nBand, &dVal, libkea::kea_64float);
        if( pbSuccess != NULL )
            *pbSuccess = 1;

        return dVal;
    }
    catch (libkea::KEAIOException &e)
    {
        if( pbSuccess != NULL )
            *pbSuccess = 0;
        return -1;
    }
}

// set the no data value
CPLErr KEARasterBand::SetNoDataValue(double dfNoData)
{
    try
    {
        this->m_pImageIO->setNoDataValue(this->nBand, &dfNoData, libkea::kea_64float);
        return CE_None;
    }
    catch (libkea::KEAIOException &e)
    {
        return CE_Failure;
    }
}

// read the attributes into a GDALAttributeTable
const GDALRasterAttributeTable *KEARasterBand::GetDefaultRAT()
{
    if( this->m_pAttributeTable == NULL )
    {
        try
        {
            if( this->m_pImageIO->attributeTablePresent(this->nBand) )
            {
                // we need to create one
                this->m_pAttributeTable = new GDALRasterAttributeTable();

                // we assume this is never NULL - creates a new one if none exists
                libkea::KEAAttributeTable *pKEATable = this->m_pImageIO->getAttributeTable(libkea::kea_att_mem, this->nBand);
    
                // create a mapping between GDAL column number and the field info
                std::vector<libkea::KEAATTField> vecKEAField;
                for( size_t nColumnIndex = 0; nColumnIndex < pKEATable->getMaxGlobalColIdx(); nColumnIndex++ )
                {
                    libkea::KEAATTField sKEAField;
                    try
                    {
                        sKEAField = pKEATable->getField(nColumnIndex);
                    }
                    catch(libkea::KEAATTException &e)
                    {
                        // pKEATable->getField raised exception because we have a missing column
                        continue;
                    }

                    GDALRATFieldType eGDALType;
                    switch( sKEAField.dataType )
                    {
                        case libkea::kea_att_bool:
                        case libkea::kea_att_int:
                            eGDALType = GFT_Integer;
                            break;
                        case libkea::kea_att_float:
                            eGDALType = GFT_Real;
                            break;
                        case libkea::kea_att_string:
                            eGDALType = GFT_String;
                            break;
                        default:
                            continue;
                            break;
                    }

                    GDALRATFieldUsage eGDALUsage;
                    if( sKEAField.usage == "PixelCount" )
                        eGDALUsage = GFU_PixelCount;
                    else if( sKEAField.usage == "Name" )
                        eGDALUsage = GFU_Name;
                    else if( sKEAField.usage == "Red" )
                        eGDALUsage = GFU_Red;
                    else if( sKEAField.usage == "Green" )
                        eGDALUsage = GFU_Green;
                    else if( sKEAField.usage == "Blue" )
                        eGDALUsage = GFU_Blue;
                    else if( sKEAField.usage == "Alpha" )
                        eGDALUsage = GFU_Alpha;
                    else
                    {
                        // don't recognise any other special names - generic column
                        eGDALUsage = GFU_Generic;
                    }

                    if( this->m_pAttributeTable->CreateColumn(sKEAField.name.c_str(), eGDALType, eGDALUsage) != CE_None )
                    {
                        CPLError( CE_Warning, CPLE_AppDefined, "Unable to create column %s", sKEAField.name.c_str() );
                        continue;
                    }

                    vecKEAField.push_back(sKEAField);
                }

                // OK now we have filled in vecKEAField we can go through each row and fill in the fields
                for( size_t nRowIndex = 0; nRowIndex < pKEATable->getSize(); nRowIndex++ )
                {
                    // get the feature
                    libkea::KEAATTFeature *pKEAFeature = pKEATable->getFeature(nRowIndex);

                    // iterate through the columns - same order as we added columns to GDAL
                    int nGDALColNum = 0;
                    for( std::vector<libkea::KEAATTField>::iterator itr = vecKEAField.begin(); itr != vecKEAField.end(); itr++ )
                    {
                        libkea::KEAATTField sKEAField = (*itr);
                        if( sKEAField.dataType == libkea::kea_att_bool )
                        {
                            bool bVal = pKEAFeature->boolFields->at(sKEAField.idx);
                            int nVal = bVal? 1 : 0; // convert to int - GDAL doesn't do bool
                            this->m_pAttributeTable->SetValue(nRowIndex, nGDALColNum, nVal);
                        }
                        else if( sKEAField.dataType == libkea::kea_att_int )
                        {
                            int nVal = pKEAFeature->intFields->at(sKEAField.idx);
                            this->m_pAttributeTable->SetValue(nRowIndex, nGDALColNum, nVal);
                        }
                        else if( sKEAField.dataType == libkea::kea_att_float )
                        {
                            double dVal = pKEAFeature->floatFields->at(sKEAField.idx);
                            this->m_pAttributeTable->SetValue(nRowIndex, nGDALColNum, dVal);
                        }
                        else
                        {
                            std::string sVal = pKEAFeature->strFields->at(sKEAField.idx);
                            this->m_pAttributeTable->SetValue(nRowIndex, nGDALColNum, sVal.c_str());
                        }
                        nGDALColNum++;
                    }
                }

                delete pKEATable;
            }
        }
        catch(libkea::KEAException &e)
        {
            CPLError( CE_Failure, CPLE_AppDefined, "Failed to read attributes: %s", e.what() );
            delete this->m_pAttributeTable;
            this->m_pAttributeTable = NULL;
        }
    }
    return this->m_pAttributeTable;
}

CPLErr KEARasterBand::SetDefaultRAT(const GDALRasterAttributeTable *poRAT)
{
    if( poRAT == NULL )
        return CE_Failure;

    try
    {
        // we assume this is never NULL - creates a new one if none exists
        libkea::KEAAttributeTable *pKEATable = this->m_pImageIO->getAttributeTable(libkea::kea_att_mem, this->nBand);

        // add rows to the table if needed
        if( pKEATable->getSize() < (size_t)poRAT->GetRowCount() )
        {
            pKEATable->addRows( poRAT->GetRowCount() - pKEATable->getSize() );
        }

        // mapping between GDAL column indices and libkea::KEAATTField
        std::map<int, libkea::KEAATTField> mapGDALtoKEA;

        for( int nGDALColumnIndex = 0; nGDALColumnIndex < poRAT->GetColumnCount(); nGDALColumnIndex++ )
        {
            const char *pszColumnName = poRAT->GetNameOfCol(nGDALColumnIndex);
            bool bExists = true;
            libkea::KEAATTField sKEAField;
            try
            {
                sKEAField = pKEATable->getField(pszColumnName);
                // if this works we assume same usage, type etc
            }
            catch(libkea::KEAATTException &e)
            {
                // doesn't exist on file - create it
                bExists = false;
            }

            if( ! bExists )
            {
                std::string strUsage = "Generic";
                switch(poRAT->GetUsageOfCol(nGDALColumnIndex))
                {
                    case GFU_PixelCount:
                        strUsage = "PixelCount";
                        break;
                    case GFU_Name:
                        strUsage = "Name";
                        break;
                    case GFU_Red:
                        strUsage = "Red";
                        break;
                    case GFU_Green:
                        strUsage = "Green";
                        break;
                    case GFU_Blue:
                        strUsage = "Blue";
                        break;
                    case GFU_Alpha:
                        strUsage = "Alpha";
                        break;
                    default:
                        // leave as "Generic"
                        break;
                }

                if(poRAT->GetTypeOfCol(nGDALColumnIndex) == GFT_Integer)
                {
                    pKEATable->addAttIntField(pszColumnName, 0, strUsage);
                }
                else if(poRAT->GetTypeOfCol(nGDALColumnIndex) == GFT_Real)
                {
                    pKEATable->addAttFloatField(pszColumnName, 0, strUsage);
                }
                else
                {
                    pKEATable->addAttStringField(pszColumnName, "", strUsage);
                }

                // assume we can just grab this now
                sKEAField = pKEATable->getField(pszColumnName);
            }
            // insert into map
            mapGDALtoKEA[nGDALColumnIndex] = sKEAField;
        }

        // go through each row to be added
        for( int nRowIndex = 0; nRowIndex < poRAT->GetRowCount(); nRowIndex++ )
        {
            // get the feature - don't need to set this back since it is a pointer to 
            // internal datastruct
            libkea::KEAATTFeature *pKEAFeature = pKEATable->getFeature(nRowIndex);

            // iterate through the map
            for( std::map<int, libkea::KEAATTField>::iterator itr = mapGDALtoKEA.begin(); itr != mapGDALtoKEA.end(); itr++ )
            {
                // get the KEA field from the map
                int nGDALColIndex = (*itr).first;
                libkea::KEAATTField sKEAField = (*itr).second;

                if( sKEAField.dataType == libkea::kea_att_bool )
                {
                    // write it as a bool even tho GDAL stores as int
                    bool bVal = poRAT->GetValueAsInt(nRowIndex, nGDALColIndex) != 0;
                    pKEAFeature->boolFields->at(sKEAField.idx) = bVal;
                }
                else if( sKEAField.dataType == libkea::kea_att_int )
                {
                    int nVal = poRAT->GetValueAsInt(nRowIndex, nGDALColIndex);
                    pKEAFeature->intFields->at(sKEAField.idx) = nVal;
                }
                else if( sKEAField.dataType == libkea::kea_att_float )
                {
                    double dVal = poRAT->GetValueAsDouble(nRowIndex, nGDALColIndex);
                    pKEAFeature->floatFields->at(sKEAField.idx) = dVal;
                }
                else
                {
                    const char *pszValue = poRAT->GetValueAsString(nRowIndex, nGDALColIndex);
                    pKEAFeature->strFields->at(sKEAField.idx) = pszValue;
                }
            }
        }

        this->m_pImageIO->setAttributeTable(pKEATable, this->nBand);
        delete pKEATable;

        // our cached attribute table object is now ouf of date
        // delete it and next call to GetDefaultRAT() will re-read it
        delete this->m_pAttributeTable;
        this->m_pAttributeTable = NULL;
    }
    catch(libkea::KEAException &e)
    {
        CPLError( CE_Failure, CPLE_AppDefined, "Failed to write attributes: %s", e.what() );
        return CE_Failure;
    }
    return CE_None;
}

GDALColorTable *KEARasterBand::GetColorTable()
{
    if( this->m_pColorTable == NULL )
    {
        try
        {
            // see if there is a suitable attribute table with color columns
            if( this->m_pImageIO->attributeTablePresent(this->nBand) )
            {
                // we assume this is never NULL - creates a new one if none exists
                libkea::KEAAttributeTable *pKEATable = this->m_pImageIO->getAttributeTable(libkea::kea_att_mem, this->nBand);
    
                // create a mapping between color entry number and the field info
                std::vector<libkea::KEAATTField> vecKEAField(4);
                for( size_t nColumnIndex = 0; nColumnIndex < pKEATable->getMaxGlobalColIdx(); nColumnIndex++ )
                {
                    libkea::KEAATTField sKEAField;
                    try
                    {
                        sKEAField = pKEATable->getField(nColumnIndex);
                    }
                    catch(libkea::KEAATTException &e)
                    {
                        // pKEATable->getField raised exception because we have a missing column
                        continue;
                    }

                    // color tables are only int as far as I am aware
                    if( sKEAField.dataType == libkea::kea_att_int )
                    {
                        // check the 'usage' column
                        // we don't check the name also (maybe we should?)
                        // store in the right place in our vector
                        if( sKEAField.usage == "Red" )
                            vecKEAField[0] = sKEAField;
                        if( sKEAField.usage == "Green" )
                            vecKEAField[1] = sKEAField;
                        if( sKEAField.usage == "Blue" )
                            vecKEAField[2] = sKEAField;
                        if( sKEAField.usage == "Alpha" )
                            vecKEAField[3] = sKEAField;
                    }

                }

                // check that we did get a valid field for each color
                // the usage field will still be empty if not set above
                bool bHaveCT = true;
                for( std::vector<libkea::KEAATTField>::iterator itr = vecKEAField.begin(); (itr != vecKEAField.end()) && bHaveCT; itr++ )
                {
                    if( (*itr).usage.empty() )
                        bHaveCT = false;
                }

                if( bHaveCT )
                {
                    // we need to create one - only do RGB palettes
                    this->m_pColorTable = new GDALColorTable(GPI_RGB);

                    // OK go through each row and fill in the fields
                    for( size_t nRowIndex = 0; nRowIndex < pKEATable->getSize(); nRowIndex++ )
                    {
                        // get the feature
                        libkea::KEAATTFeature *pKEAFeature = pKEATable->getFeature(nRowIndex);

                        GDALColorEntry colorEntry;
                        colorEntry.c1 = pKEAFeature->intFields->at(vecKEAField[0].idx);
                        colorEntry.c2 = pKEAFeature->intFields->at(vecKEAField[1].idx);
                        colorEntry.c3 = pKEAFeature->intFields->at(vecKEAField[2].idx);
                        colorEntry.c4 = pKEAFeature->intFields->at(vecKEAField[3].idx);

                        this->m_pColorTable->SetColorEntry(nRowIndex, &colorEntry);
                    }
                }

                delete pKEATable;
            }
        }
        catch(libkea::KEAException &e)
        {
            CPLError( CE_Failure, CPLE_AppDefined, "Failed to read color table: %s", e.what() );
            delete this->m_pColorTable;
            this->m_pColorTable = NULL;
        }
    }
    return this->m_pColorTable;
}

CPLErr KEARasterBand::SetColorTable(GDALColorTable *poCT)
{
    if( poCT == NULL )
        return CE_Failure;

    try
    {
        // we assume this is never NULL - creates a new one if none exists
        libkea::KEAAttributeTable *pKEATable = this->m_pImageIO->getAttributeTable(libkea::kea_att_mem, this->nBand);

        // add rows to the table if needed
        if( pKEATable->getSize() < (size_t)poCT->GetColorEntryCount() )
        {
            pKEATable->addRows( poCT->GetColorEntryCount() - pKEATable->getSize() );
        }

        // create a mapping between color entry number and the field info
        std::vector<libkea::KEAATTField> vecKEAField(4);
        for( size_t nColumnIndex = 0; nColumnIndex < pKEATable->getMaxGlobalColIdx(); nColumnIndex++ )
        {
            libkea::KEAATTField sKEAField;
            try
            {
                sKEAField = pKEATable->getField(nColumnIndex);
            }
            catch(libkea::KEAATTException &e)
            {
                // pKEATable->getField raised exception because we have a missing column
                continue;
            }

            // color tables are only int as far as I am aware
            if( sKEAField.dataType == libkea::kea_att_int )
            {
                // check the 'usage' column
                // we don't check the name also (maybe we should?)
                // store in the right place in our vector
                if( sKEAField.usage == "Red" )
                    vecKEAField[0] = sKEAField;
                else if( sKEAField.usage == "Green" )
                    vecKEAField[1] = sKEAField;
                else if( sKEAField.usage == "Blue" )
                    vecKEAField[2] = sKEAField;
                else if( sKEAField.usage == "Alpha" )
                    vecKEAField[3] = sKEAField;
            }
        }

        // create any missing fields
        if( vecKEAField[0].usage.empty() )
        {
            pKEATable->addAttIntField("Red", 0, "Red");
            vecKEAField[0] = pKEATable->getField("Red");
        }
        if( vecKEAField[1].usage.empty() )
        {
            pKEATable->addAttIntField("Green", 0, "Green");
            vecKEAField[1] = pKEATable->getField("Green");
        }
        if( vecKEAField[2].usage.empty() )
        {
            pKEATable->addAttIntField("Blue", 0, "Blue");
            vecKEAField[2] = pKEATable->getField("Blue");
        }
        if( vecKEAField[3].usage.empty() )
        {
            pKEATable->addAttIntField("Alpha", 0, "Alpha");
            vecKEAField[3] = pKEATable->getField("Alpha");
        }

        // go through each row to be added
        for( int nRowIndex = 0; nRowIndex < poCT->GetColorEntryCount(); nRowIndex++ )
        {
            // get the feature - don't need to set this back since it is a pointer to 
            // internal datastruct
            libkea::KEAATTFeature *pKEAFeature = pKEATable->getFeature(nRowIndex);

            // get the GDAL entry - as RGB to be sure
            GDALColorEntry colorEntry;
            if( poCT->GetColorEntryAsRGB(nRowIndex, &colorEntry) )
            {
                // set the value
                pKEAFeature->intFields->at(vecKEAField[0].idx) = colorEntry.c1;
                pKEAFeature->intFields->at(vecKEAField[1].idx) = colorEntry.c2;
                pKEAFeature->intFields->at(vecKEAField[2].idx) = colorEntry.c3;
                pKEAFeature->intFields->at(vecKEAField[3].idx) = colorEntry.c4;
            }
        }

        this->m_pImageIO->setAttributeTable(pKEATable, this->nBand);
        delete pKEATable;

        // replace our color table with the one passed in
        // unlike attributes there are no extra fields present in the file etc
        // so should be safe to do this
        delete this->m_pColorTable;
        this->m_pColorTable = poCT->Clone();
    }
    catch(libkea::KEAException &e)
    {
        CPLError( CE_Failure, CPLE_AppDefined, "Failed to write color table: %s", e.what() );
        return CE_Failure;
    }
    return CE_None;
}

GDALColorInterp KEARasterBand::GetColorInterpretation()
{
    libkea::KEALayerUsage keainterp;
    try
    {
        this->m_pImageIO->getImageBandUsage(this->nBand);
    }
    catch(libkea::KEAException &e)
    {
        return GCI_GrayIndex;
    }

    GDALColorInterp gdalinterp;
    switch(keainterp)
    {
        case libkea::kea_generic:
        case libkea::kea_greyindex:
            gdalinterp = GCI_GrayIndex;
            break;
        case libkea::kea_paletteindex:
            gdalinterp = GCI_PaletteIndex;
            break;
        case libkea::kea_redband:
            gdalinterp = GCI_GreenBand;
            break;
        case libkea::kea_blueband:
            gdalinterp = GCI_BlueBand;
            break;
        case libkea::kea_alphaband:
            gdalinterp = GCI_AlphaBand;
            break;
        case libkea::kea_hueband:
            gdalinterp = GCI_HueBand;
            break;
        case libkea::kea_saturationband:
            gdalinterp = GCI_SaturationBand;
            break;
        case libkea::kea_lightnessband:
            gdalinterp = GCI_LightnessBand;
            break;
        case libkea::kea_cyanband:
            gdalinterp = GCI_CyanBand;
            break;
        case libkea::kea_magentaband:
            gdalinterp = GCI_MagentaBand;
            break;
        case libkea::kea_yellowband:
            gdalinterp = GCI_YellowBand;
            break;
        case libkea::kea_blackband:
            gdalinterp = GCI_BlackBand;
            break;
        case libkea::kea_ycbcr_yband:
            gdalinterp = GCI_YCbCr_YBand;
            break;
        case libkea::kea_ycbcr_cbband:
            gdalinterp = GCI_YCbCr_CbBand;
            break;
        case libkea::kea_ycbcr_crband:
            gdalinterp = GCI_YCbCr_CrBand;
            break;
        default:
            gdalinterp = GCI_GrayIndex;
            break;
    }
    return gdalinterp;
}

CPLErr KEARasterBand::SetColorInterpretation(GDALColorInterp gdalinterp)
{
    libkea::KEALayerUsage keainterp;
    switch(gdalinterp)
    {
        case GCI_GrayIndex:
            keainterp = libkea::kea_greyindex;
            break;
        case GCI_PaletteIndex:
            keainterp = libkea::kea_paletteindex;
            break;
        case GCI_RedBand:
            keainterp = libkea::kea_redband;
            break;
        case GCI_GreenBand:
            keainterp = libkea::kea_greenband;
            break;
        case GCI_BlueBand:
            keainterp = libkea::kea_blueband;
            break;
        case GCI_AlphaBand:
            keainterp = libkea::kea_alphaband;
            break;
        case GCI_HueBand:
            keainterp = libkea::kea_hueband;
            break;
        case GCI_SaturationBand:
            keainterp = libkea::kea_saturationband;
            break;
        case GCI_LightnessBand:
            keainterp = libkea::kea_lightnessband;
            break;
        case GCI_CyanBand:
            keainterp = libkea::kea_cyanband;
            break;
        case GCI_MagentaBand:
            keainterp = libkea::kea_magentaband;
            break;
        case GCI_YellowBand:
            keainterp = libkea::kea_yellowband;
            break;
        case GCI_BlackBand:
            keainterp = libkea::kea_blackband;
            break;
        case GCI_YCbCr_YBand:
            keainterp = libkea::kea_ycbcr_yband;
            break;
        case GCI_YCbCr_CbBand:
            keainterp = libkea::kea_ycbcr_cbband;
            break;
        case GCI_YCbCr_CrBand:
            keainterp = libkea::kea_ycbcr_crband;
            break;
        default:
            keainterp = libkea::kea_greyindex;
            break;
    }

    try
    {
        this->m_pImageIO->setImageBandUsage(this->nBand, keainterp);
    }
    catch(libkea::KEAException &e)
    {
        // do nothing? The docs say CE_Failure only if unsupporte by format
    }
    return CE_None;
}

// clean up our overview objects
void KEARasterBand::deleteOverviewObjects()
{
    // deletes the objects - not the overviews themselves
    int nCount;
    for( nCount = 0; nCount < m_nOverviews; nCount++ )
    {
        delete m_panOverviewBands[nCount];
    }
    CPLFree(m_panOverviewBands);
    m_panOverviewBands = NULL;
    m_nOverviews = 0;
}

// read in any overviews in the file into our array of objects
void KEARasterBand::readExistingOverviews()
{
    // delete any existing overview bands
    this->deleteOverviewObjects();

    m_nOverviews = this->m_pImageIO->getNumOfOverviews(this->nBand);
    m_panOverviewBands = (KEAOverview**)CPLMalloc(sizeof(KEAOverview*) * m_nOverviews);

    unsigned long nXSize, nYSize;    
    for( int nCount = 0; nCount < m_nOverviews; nCount++ )
    {
        this->m_pImageIO->getOverviewSize(this->nBand, nCount + 1, &nXSize, &nYSize);
        m_panOverviewBands[nCount] = new KEAOverview((KEADataset*)this->poDS, this->nBand, GA_ReadOnly,
                                        this->m_pImageIO, this->m_pnRefCount, nCount + 1, nXSize, nYSize);
    }
}

// number of overviews
int KEARasterBand::GetOverviewCount()
{
    return m_nOverviews;
}

// get a given overview
GDALRasterBand* KEARasterBand::GetOverview(int nOverview)
{
    if( nOverview >= m_nOverviews )
    {
        return NULL;
    }
    else
    {
        return m_panOverviewBands[nOverview];
    }
}

void KEARasterBand::SetHistogramFromMetadata(const char *pszHistogram)
{
    try
    {
        // we assume this is never NULL - creates a new one if none exists
        libkea::KEAAttributeTable *pKEATable = this->m_pImageIO->getAttributeTable(libkea::kea_att_mem, this->nBand);

        // how many elements in pszHistogram? '|' seperated
        size_t nItems = 0, nIndex = 0, nStartIndex;
        while( pszHistogram[nIndex] != '\0' )
        {
            nStartIndex = nIndex;
            while( ( pszHistogram[nIndex] != '|' ) && ( pszHistogram[nIndex] != '\0' ) )
                nIndex++;
            if( nStartIndex != nIndex )
                nItems++;
            if( pszHistogram[nIndex] != '\0' )
                nIndex++;
        }

        // add rows to the table if needed
        if( pKEATable->getSize() < nItems )
        {
            pKEATable->addRows( nItems - pKEATable->getSize() );
        }
    
        // create a mapping between histogram entry and the field info
        bool bFoundHisto = false;
        libkea::KEAATTField sKEAField;
        for( size_t nColumnIndex = 0; (nColumnIndex < pKEATable->getMaxGlobalColIdx()) && !bFoundHisto; nColumnIndex++ )
        {
            try
            {
                sKEAField = pKEATable->getField(nColumnIndex);
            }
            catch(libkea::KEAATTException &e)
            {
                // pKEATable->getField raised exception because we have a missing column
                continue;
            }

            // does it look like the histogram column?
            if( ( sKEAField.dataType == libkea::kea_att_int ) && ( sKEAField.usage == "PixelCount" ) 
                && ( sKEAField.name == "Histogram" ) )
            {
                bFoundHisto = true;
            }
        }

        if( !bFoundHisto )
        {
            // need to create it
            pKEATable->addAttIntField("Histogram", 0, "PixelCount");
            sKEAField = pKEATable->getField("Histogram");
        }

        // ok go through pszHistogram for real and insert items
        nIndex = 0;
        size_t nRowIndex = 0;
        char szBuf[12];
        int nBufIndex;
        while( pszHistogram[nIndex] != '\0' )
        {
            nStartIndex = nIndex;
            nBufIndex = 0;
            while( ( pszHistogram[nIndex] != '|' ) && ( pszHistogram[nIndex] != '\0' ) )
            {
                szBuf[nBufIndex] = pszHistogram[nIndex];
                nBufIndex++;
                nIndex++;
            }
            if( nStartIndex != nIndex )
            {
                szBuf[nBufIndex] = '\0';
                libkea::KEAATTFeature *pKEAFeature = pKEATable->getFeature(nRowIndex);
                pKEAFeature->intFields->at(sKEAField.idx) = atol( szBuf );
                nRowIndex++;
            }
            if( pszHistogram[nIndex] != '\0' )
                nIndex++;
        }
        this->m_pImageIO->setAttributeTable(pKEATable, this->nBand);
        delete pKEATable;
    }
    catch(libkea::KEAException &e)
    {
        CPLError( CE_Failure, CPLE_AppDefined, "Failed to write histogram table: %s", e.what() );
    }
}

std::string KEARasterBand::GetHistogramAsMetadata()
{
    std::string sHistogram;
    try
    {
        // see if there is a suitable attribute table with color columns
        if( this->m_pImageIO->attributeTablePresent(this->nBand) )
        {
            // we assume this is never NULL - creates a new one if none exists
            libkea::KEAAttributeTable *pKEATable = this->m_pImageIO->getAttributeTable(libkea::kea_att_mem, this->nBand);

            // create a mapping between histogram entry and the field info
            bool bFoundHisto = false;
            libkea::KEAATTField sKEAField;
            for( size_t nColumnIndex = 0; (nColumnIndex < pKEATable->getMaxGlobalColIdx()) && !bFoundHisto; nColumnIndex++ )
            {
                try
                {
                    sKEAField = pKEATable->getField(nColumnIndex);
                }
                catch(libkea::KEAATTException &e)
                {
                    // pKEATable->getField raised exception because we have a missing column
                    continue;
                }

                // does it look like the histogram column?
                if( ( sKEAField.dataType == libkea::kea_att_int ) && ( sKEAField.usage == "PixelCount" ) 
                    && ( sKEAField.name == "Histogram" ) )
                {
                    bFoundHisto = true;
                }
            }
            // if we found a histogram column, read it
            char szBuf[24];
            if( bFoundHisto )
            {
                // OK go through each row and fill in the fields
                for( size_t nRowIndex = 0; nRowIndex < pKEATable->getSize(); nRowIndex++ )
                {
                    // get the feature
                    libkea::KEAATTFeature *pKEAFeature = pKEATable->getFeature(nRowIndex);
                    long nValue = pKEAFeature->intFields->at(sKEAField.idx);
                    sprintf( szBuf, "%ld|", nValue );
                    sHistogram.append(szBuf); // dunno how fast this is...
                }
            }

            delete pKEATable;
        }
    }
    catch(libkea::KEAException &e)
    {
        CPLError( CE_Failure, CPLE_AppDefined, "Failed to read histogram table: %s", e.what() );
        sHistogram = "";
    }
    return sHistogram;
}
