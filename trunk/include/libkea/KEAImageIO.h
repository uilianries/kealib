/*
 *  KEAImageIO.h
 *  LibKEA
 *
 *  Created by Pete Bunting on 02/07/2012.
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

#ifndef KEAImageIO_H
#define KEAImageIO_H

#include <iostream>
#include <string>
#include <vector>

#include "H5Cpp.h"

#include "libkea/KEACommon.h"
#include "libkea/KEAException.h"
#include "libkea/KEAAttributeTable.h"
#include "libkea/KEAAttributeTableInMem.h"

namespace libkea{
        
    class DllExport KEAImageIO
    {
    public:
        KEAImageIO();
                
        void openKEAImageHeader(H5::H5File *keaImgH5File)throw(KEAIOException);
        
        void writeImageBlock2Band(unsigned int band, void *data, unsigned long xPxlOff, unsigned long yPxlOff, unsigned long xSizeOut, unsigned long ySizeOut, unsigned long xSizeBuf, unsigned long ySizeBuf, KEADataType inDataType)throw(KEAIOException);
        void readImageBlock2Band(unsigned int band, void *data, unsigned long xPxlOff, unsigned long yPxlOff, unsigned long xSizeIn, unsigned long ySizeIn, unsigned long xSizeBuf, unsigned long ySizeBuf, KEADataType inDataType)throw(KEAIOException);

        void setImageMetaData(std::string name, std::string value)throw(KEAIOException);
        std::string getImageMetaData(std::string name)throw(KEAIOException);
        std::vector<std::string> getImageMetaDataNames()throw(KEAIOException);
        std::vector< std::pair<std::string, std::string> > getImageMetaData()throw(KEAIOException);
        void setImageMetaData(std::vector< std::pair<std::string, std::string> > data)throw(KEAIOException);
        
        void setImageBandMetaData(unsigned int band, std::string name, std::string value)throw(KEAIOException);
        std::string getImageBandMetaData(unsigned int band, std::string name)throw(KEAIOException);
        std::vector<std::string> getImageBandMetaDataNames(unsigned int band)throw(KEAIOException);
        std::vector< std::pair<std::string, std::string> > getImageBandMetaData(unsigned int band)throw(KEAIOException);
        void setImageBandMetaData(unsigned int band, std::vector< std::pair<std::string, std::string> > data)throw(KEAIOException);
        
        void setImageBandDescription(unsigned int band, std::string description)throw(KEAIOException);
        std::string getImageBandDescription(unsigned int band)throw(KEAIOException);
        
        void setNoDataValue(unsigned int band, void *data, KEADataType inDataType)throw(KEAIOException);
        void getNoDataValue(unsigned int band, void *data, KEADataType inDataType)throw(KEAIOException);
        
        void setSpatialInfo(KEAImageSpatialInfo *spatialInfo)throw(KEAIOException);
        KEAImageSpatialInfo* getSpatialInfo() throw(KEAIOException);
                
        unsigned int getNumOfImageBands() throw(KEAIOException);
        
        unsigned int getImageBlockSize(unsigned int band) throw(KEAIOException);
        
        KEADataType getImageBandDataType(unsigned int band) throw(KEAIOException);
        
        void setImageBandLayerType(unsigned int band, KEALayerType imgLayerType) throw(KEAIOException);
        KEALayerType getImageBandLayerType(unsigned int band) throw(KEAIOException);
        
        void setImageBandClrInterp(unsigned int band, KEABandClrInterp imgLayerClrInterp) throw(KEAIOException);
        KEABandClrInterp getImageBandClrInterp(unsigned int band) throw(KEAIOException);
        
        void createOverview(unsigned int band, unsigned int overview, unsigned long xSize, unsigned long ySize) throw(KEAIOException);
        void removeOverview(unsigned int band, unsigned int overview) throw(KEAIOException);
        unsigned int getOverviewBlockSize(unsigned int band, unsigned int overview) throw(KEAIOException);
        void writeToOverview(unsigned int band, unsigned int overview, void *data, unsigned long xPxlOff, unsigned long yPxlOff, unsigned long xSizeOut, unsigned long ySizeOut, unsigned long xSizeBuf, unsigned long ySizeBuf, KEADataType inDataType) throw(KEAIOException);
        void readFromOverview(unsigned int band, unsigned int overview, void *data, unsigned long xPxlOff, unsigned long yPxlOff, unsigned long xSizeIn, unsigned long ySizeIn, unsigned long xSizeBuf, unsigned long ySizeBuf, KEADataType inDataType) throw(KEAIOException);
        unsigned int getNumOfOverviews(unsigned int band) throw(KEAIOException);
        void getOverviewSize(unsigned int band, unsigned int overview, unsigned long *xSize, unsigned long *ySize) throw(KEAIOException);
                
        KEAAttributeTable* getAttributeTable(KEAATTType type, unsigned int band) throw(KEAATTException, KEAIOException);
        void setAttributeTable(KEAAttributeTable* att, unsigned int band, unsigned int chunkSize=KEA_ATT_CHUNK_SIZE, unsigned int deflate=KEA_DEFLATE) throw(KEAATTException, KEAIOException);
        bool attributeTablePresent(unsigned int band);
        
        void close()throw(KEAIOException);
        
        static H5::H5File* createKEAImage(std::string fileName, KEADataType dataType, unsigned int xSize, unsigned int ySize, unsigned int numImgBands, std::vector<std::string> *bandDescrips=NULL, KEAImageSpatialInfo *spatialInfo=NULL, unsigned int imageBlockSize=KEA_IMAGE_CHUNK_SIZE, unsigned int attBlockSize=KEA_ATT_CHUNK_SIZE, int mdcElmts=KEA_MDC_NELMTS, hsize_t rdccNElmts=KEA_RDCC_NELMTS, hsize_t rdccNBytes=KEA_RDCC_NBYTES, double rdccW0=KEA_RDCC_W0, hsize_t sieveBuf=KEA_SIEVE_BUF, hsize_t metaBlockSize=KEA_META_BLOCKSIZE, unsigned int deflate=KEA_DEFLATE)throw(KEAIOException);
        static bool isKEAImage(std::string fileName)throw(KEAIOException);
        static H5::H5File* openKeaH5RW(std::string fileName, int mdcElmts=KEA_MDC_NELMTS, hsize_t rdccNElmts=KEA_RDCC_NELMTS, hsize_t rdccNBytes=KEA_RDCC_NBYTES, double rdccW0=KEA_RDCC_W0, hsize_t sieveBuf=KEA_SIEVE_BUF, hsize_t metaBlockSize=KEA_META_BLOCKSIZE)throw(KEAIOException);
        static H5::H5File* openKeaH5RDOnly(std::string fileName, int mdcElmts=KEA_MDC_NELMTS, hsize_t rdccNElmts=KEA_RDCC_NELMTS, hsize_t rdccNBytes=KEA_RDCC_NBYTES, double rdccW0=KEA_RDCC_W0, hsize_t sieveBuf=KEA_SIEVE_BUF, hsize_t metaBlockSize=KEA_META_BLOCKSIZE)throw(KEAIOException);
        virtual ~KEAImageIO();
    protected:
        /**
         * Converts KEA datatypes to the respective standard HDF5 datatypes.
         */
        static H5::DataType convertDatatypeKeaToH5STD(
                const KEADataType dataType) throw(KEAIOException);

        /**
         * Converts KEA datatypes to the respective native HDF5 datatypes.
         */
        static H5::DataType convertDatatypeKeaToH5Native(
                const KEADataType dataType) throw(KEAIOException);

        /**
         * Updates the number of image bands in the file metadata.
         */
        virtual void updateNumImgBands() const throw(KEAIOException);

        bool fileOpen;
        H5::H5File *keaImgFile;
        KEAImageSpatialInfo *spatialInfoFile;
        unsigned int numImgBands;
    };
    
}


#endif




