/*
 *  KEADataset.h
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

#ifndef KEADATASET_H
#define KEADATASET_H

#include "gdal_pam.h"
#include "libkea/KEAImageIO.h"

// class that implements a GDAL dataset
class KEADataset : public GDALPamDataset
{
public:
    // constructor/destructor
    KEADataset( H5::H5File *keaImgH5File, GDALAccess eAccess );
    ~KEADataset();
    
    // static methods that handle open and creation
    // the driver class has pointers to these
    static GDALDataset *Open( GDALOpenInfo * );
    static int Identify( GDALOpenInfo * poOpenInfo );
    static GDALDataset *Create( const char * pszFilename,
                                  int nXSize, int nYSize, int nBands,
                                  GDALDataType eType,
                                  char **  papszParmList );
    static GDALDataset *CreateCopy( const char * pszFilename, GDALDataset *pSrcDs,
                                int bStrict, char **  papszParmList, 
                                GDALProgressFunc pfnProgress, void *pProgressData );

    // virtual methods for dealing with transform and projection
    CPLErr      GetGeoTransform( double * padfTransform );
    const char *GetProjectionRef();

    CPLErr  SetGeoTransform (double *padfTransform );
    CPLErr SetProjection( const char *pszWKT );

    // method to get a pointer to the imageio class
    void *GetInternalHandle (const char *);

    // virtual methods for dealing with metadata
    CPLErr SetMetadataItem (const char *pszName, const char *pszValue, const char *pszDomain="");
    const char *GetMetadataItem (const char *pszName, const char *pszDomain="");

    char **GetMetadata(const char *pszDomain="");
    CPLErr SetMetadata(char **papszMetadata, const char *pszDomain="");

protected:
    // this method builds overviews for the specified bands. 
    virtual CPLErr IBuildOverviews(const char *pszResampling, int nOverviews, int *panOverviewList, 
                                    int nListBands, int *panBandList, GDALProgressFunc pfnProgress, 
                                    void *pProgressData);

    // internal method to update m_papszMetadataList
    void UpdateMetadataList();

private:
    // pointer to KEAImageIO class and the refcount for it
    libkea::KEAImageIO  *m_pImageIO;
    int                 *m_pnRefcount;
    char               **m_papszMetadataList; // CSLStringList for metadata

};

// conversion functions
GDALDataType KEA_to_GDAL_Type( libkea::KEADataType keaType );
libkea::KEADataType GDAL_to_KEA_Type( GDALDataType gdalType );

#endif //KEADATASET_H
