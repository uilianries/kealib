
#include "KEABand.h"

KEARasterBand::KEARasterBand( KEADataset *pDataset, int nBand, libkea::KEAImageIO *pImageIO, int *pRefCount )
{
    this->poDS = pDataset;
    this->nBand = nBand;
    this->eDataType = KEA_to_GDAL_Type( pImageIO->getImageDataType() );
    this->nBlockXSize = pImageIO->getImageBlockSize();
    this->nBlockYSize = pImageIO->getImageBlockSize();
    this->m_nXSize = this->poDS->GetRasterXSize();
    this->m_nYSize = this->poDS->GetRasterYSize();

    this->m_pImageIO = pImageIO;
    this->m_pnRefCount = pRefCount;
    // increment the refcount as we now have a reference to imageio
    (*this->m_pnRefCount)++;

    // overviews
    m_nOverviews = 0;
    m_panOverviewBands = NULL;
}

KEARasterBand::~KEARasterBand()
{
    // delete any overview bands
    for( int nCount = 0; nCount < m_nOverviews; nCount++ )
    {
        delete m_panOverviewBands[nCount];
    }
    CPLFree(m_panOverviewBands);

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

void KEARasterBand::CreateOverviews(int nOverviews, int *panOverviewList)
{
   // delete any existing overview bands
    int nCount;
    for( nCount = 0; nCount < m_nOverviews; nCount++ )
    {
        delete m_panOverviewBands[nCount];
    }
    CPLFree(m_panOverviewBands);

    m_panOverviewBands = CPLMalloc(sizeof(KEAOverview*) * nOverviews);
    m_nOverviews = nOverviews;

    int nFactor, nXSize, nYSize;
    for( nCount = 0; nCount < m_nOverviews; nCount++ )
    {
        nFactor = panOverviewList[nCount];
        nXSize = this->m_nXSize / nFactor;
        nYSize = this->m_nYSize / nFactor;

        this->m_pImageIO->createOverview(this->m_nBand, nCount + 1, nXSize, nYSize);

        m_panOverviewBands[nCount] = new KEAOverview(this->poDS, this->m_nBand, 
                                        this->m_pImageIO, this->m_pnRefCount, nCount + 1, nXSize, nYSize);
    }
}


CPLErr KEARasterBand::IReadBlock( int nBlockXOff, int nBlockYOff, void * pImage )
{
    try
    {
        // GDAL deals in blocks - if we are at the end of a row
        // we need to adjust the amount read so we don't go over the edge
        int xsize = this->nBlockXSize;
        int xtotalsize = this->nBlockXSize * (nBlockXOff + 1);
        if( xtotalsize > m_nXSize )
        {
            xsize -= (xtotalsize - m_nXSize);
        }
        int ysize = this->nBlockYSize;
        int ytotalsize = this->nBlockYSize * (nBlockYOff + 1);
        if( ytotalsize > m_nYSize )
        {
            ysize -= (ytotalsize - m_nYSize);
        }
        this->m_pImageIO->readImageBlock2Band( this->nBand, pImage, this->nBlockXSize * nBlockXOff,
                                            this->nBlockYSize * nBlockYOff,
                                            xsize, ysize, 
                                            this->m_pImageIO->getImageDataType() );
        return CE_None;
    }
    catch (libkea::KEAIOException &e)
    {
        CPLError( CE_Failure, CPLE_AppDefined,
                "Failed to read file: %s", e.what() );
        return CE_Failure;
    }
}

CPLErr KEARasterBand::IWriteBlock( int nBlockXOff, int nBlockYOff, void * pImage )
{
    try
    {
        // GDAL deals in blocks - if we are at the end of a row
        // we need to adjust the amount written so we don't go over the edge
        int xsize = this->nBlockXSize;
        int xtotalsize = this->nBlockXSize * (nBlockXOff + 1);
        if( xtotalsize > m_nXSize )
        {
            xsize -= (xtotalsize - m_nXSize);
        }
        int ysize = this->nBlockYSize;
        int ytotalsize = this->nBlockYSize * (nBlockYOff + 1);
        if( ytotalsize > m_nYSize )
        {
            ysize -= (ytotalsize - m_nYSize);
        }

        this->m_pImageIO->writeImageBlock2Band( this->nBand, pImage, this->nBlockXSize * nBlockXOff,
                                            this->nBlockYSize * nBlockYOff,
                                            xsize, ysize, 
                                            this->m_pImageIO->getImageDataType() );
        return CE_None;
    }
    catch (libkea::KEAIOException &e)
    {
        CPLError( CE_Failure, CPLE_AppDefined,
                "Failed to write file: %s", e.what() );
        return CE_Failure;
    }
}

