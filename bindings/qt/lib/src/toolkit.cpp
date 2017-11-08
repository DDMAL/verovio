/////////////////////////////////////////////////////////////////////////////
// Name:        toolkit.cpp
// Author:      Jonathan Schluessler
// Created:     2017
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "vrvqt/toolkit.h"

//----------------------------------------------------------------------------

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QUrl>

namespace vrvQt {
Toolkit::Toolkit()
    : m_verovioToolkit(false)
    , m_displayWidth(m_verovioToolkit.GetPageWidth())
    , m_displayHeight(m_verovioToolkit.GetPageHeight())
{
    connect(this, SIGNAL(documentLayoutInvalidated()), this, SLOT(documentRelayout()), Qt::QueuedConnection);
    connect(this, SIGNAL(fileNameInvalidated()), this, SLOT(readFile()), Qt::QueuedConnection);
    connect(this, SIGNAL(fileContentInvalidated()), this, SLOT(reloadData()), Qt::QueuedConnection);
}

int Toolkit::adjustedDisplayHeightForPage(int page)
{
    if (!m_verovioToolkit.GetAdjustPageHeight()) return m_displayHeight;

    Q_ASSERT(page <= m_adjustedDisplayHeights.count());

    int defaultPageHeight = 1000;

    if (m_adjustedDisplayHeights[page - 1] != 0) {
        return m_adjustedDisplayHeights[page - 1];
    }
    else {
        int averagePageHeight = 0;
        int num = 0;
        for (auto height : m_adjustedDisplayHeights) {
            if (height != 0) {
                averagePageHeight += height;
                num++;
            }
        }
        if (num == 0) return defaultPageHeight;
        return averagePageHeight / num;
    }
}

void Toolkit::setAdjustedDisplayHeightForPage(int pageNumber, int pageHeight)
{
    if (m_verovioToolkit.GetAdjustPageHeight()) {
        Q_ASSERT(pageNumber <= m_adjustedDisplayHeights.count());
        m_adjustedDisplayHeights[pageNumber - 1] = pageHeight;
    }
}

void Toolkit::renderPage(int page, vrv::DeviceContext *deviceContext)
{
    if (m_hasValidData) {
        m_verovioToolkit.RenderToDeviceContext(page, deviceContext);
    }
}

void Toolkit::setHasValidData(bool hasValidData)
{
    if (m_hasValidData != hasValidData) {
        m_hasValidData = hasValidData;
        emit hasValidDataChanged(hasValidData);
    }
}

void Toolkit::setPageCount(int pageCount)
{
    if (m_pageCount != pageCount) {
        m_pageCount = pageCount;
        m_adjustedDisplayHeights.fill(0, pageCount);

        emit pageCountChanged(pageCount);
    }
}

void Toolkit::setFileName(QString fileName)
{
    // file-names from QML start with file://
    if (fileName.startsWith("file://")) {
        fileName = QUrl(fileName).toLocalFile();
    }

    if (m_fileName != fileName) {
        m_fileName = fileName;
        requestReadFile();
    }
}

void Toolkit::setMusicFontName(QString musicFont)
{
    if (m_musicFontName != musicFont) {
        m_musicFontName = musicFont;
        m_fontInitDone = false;
        initFont();
        requestDocumentRelayout();
    }
}

void Toolkit::setMusicFontPath(QString musicFontPath)
{
    if (m_musicFontPath != musicFontPath) {
        m_musicFontPath = musicFontPath;
        m_fontInitDone = false;
        initFont();
        requestDocumentRelayout();
    }
}

void Toolkit::setVerovioTextFontPath(QString verovioTextFontPath)
{
    if (m_verovioTextFontPath != verovioTextFontPath) {
        m_verovioTextFontPath = verovioTextFontPath;
        m_fontInitDone = false;
        initFont();
        requestDocumentRelayout();
    }
}

void Toolkit::setDisplayWidth(int pageWidth)
{
    if (m_displayWidth != pageWidth) {
        m_displayWidth = pageWidth;
        requestDocumentRelayout();
    }
}

void Toolkit::setDisplayHeight(int pageHeight)
{
    if (m_displayHeight != pageHeight) {
        m_displayHeight = pageHeight;
        requestDocumentRelayout();
    }
}

void Toolkit::setScale(int scale)
{
    if (m_verovioToolkit.GetScale() != scale) {
        m_verovioToolkit.SetScale(scale);
        requestDocumentRelayout();
    }
}

void Toolkit::setBorder(int border)
{
    if (m_verovioToolkit.GetBorder() != border) {
        m_verovioToolkit.SetAdjustPageHeight(border);
        requestDocumentRelayout();
    }
}

void Toolkit::setAdjustPageHeight(bool adjustPageHeight)
{
    if (m_verovioToolkit.GetAdjustPageHeight() != adjustPageHeight) {
        m_verovioToolkit.SetAdjustPageHeight(adjustPageHeight);
        requestDocumentRelayout();
    }
}

void Toolkit::setNoLayout(bool noLayout)
{
    if (m_verovioToolkit.GetNoLayout() != noLayout) {
        m_verovioToolkit.SetNoLayout(noLayout);
        // "no layout" is used in LoadData
        requestReloadData();
    }
}

void Toolkit::setIgnoreLayout(bool ignoreLayout)
{
    if (m_verovioToolkit.GetIgnoreLayout() != ignoreLayout) {
        m_verovioToolkit.SetIgnoreLayout(ignoreLayout);
        // "ignore layout" is used in LoadData
        requestReloadData();
    }
}

void Toolkit::setFileContent(QString fileContent)
{
    if (m_fileContent != fileContent) {
        m_fileContent = fileContent;
        requestReloadData();
    }
}

void Toolkit::setResourcesDataPath(QString resourcesDataPath)
{
    if (m_resourcesDataPath != resourcesDataPath) {
        m_resourcesDataPath = resourcesDataPath;
        bool success = m_verovioToolkit.SetResourcePath(resourcesDataPath.toStdString());

        if (!success) {
            m_resourcesDataInitialized = false;
            qWarning() << "The music font could not be loaded; please check the contents of the resource directory.";
            return;
        }
        m_resourcesDataInitialized = true;
        requestReloadData();
    }
}

void Toolkit::setSpacingStaff(int spacingStaff)
{
    if (m_verovioToolkit.GetSpacingStaff() != spacingStaff) {
        m_verovioToolkit.SetSpacingStaff(spacingStaff);
        requestDocumentRelayout();
    }
}

void Toolkit::setSpacingSystem(int spacingSystem)
{
    if (m_verovioToolkit.GetSpacingSystem() != spacingSystem) {
        m_verovioToolkit.SetSpacingSystem(spacingSystem);
        requestDocumentRelayout();
    }
}

bool Toolkit::addFont(QString fontFilePath)
{
    int fontId = QFontDatabase::addApplicationFont(fontFilePath);
    if (fontId == -1) {
        QFileInfo fontFile(fontFilePath);
        if (!fontFile.exists() || !fontFile.isFile()) {
            qWarning() << "Font file" << fontFilePath << "does not exist!";
        }
        else {
            qWarning() << "Could not add font" << fontFilePath << "(file exists)";
        }
        return false;
    }
    return true;
}

bool Toolkit::initFont()
{
    if (m_musicFontName.isEmpty() || m_musicFontPath.isEmpty() || m_verovioTextFontPath.isEmpty()) {
        return false;
    }

    if (m_fontInitDone) return true;
    m_fontInitDone = true;

    m_verovioToolkit.SetFont(m_musicFontName.toStdString());

    addFont(m_musicFontPath);
    addFont(m_verovioTextFontPath);

    return true;
}

void Toolkit::requestReadFile()
{
    if (!m_readFileRequested) {
        m_readFileRequested = true;
        emit fileNameInvalidated();
    }
}

void Toolkit::requestReloadData()
{
    if (!m_reloadDataRequested) {
        m_reloadDataRequested = true;
        emit fileContentInvalidated();
    }
}

void Toolkit::requestDocumentRelayout()
{
    if (!m_documentRelayoutRequested) {
        m_documentRelayoutRequested = true;
        emit documentLayoutInvalidated();
    }
}

void Toolkit::readFile()
{
    bool success = false;
    m_readFileRequested = false;

    QFile inputFile(m_fileName);
    if (inputFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&inputFile);
        m_fileContent = stream.readAll();
        success = true;
    }

    if (success) {
        requestReloadData();
    }
}

void Toolkit::reloadData()
{
    m_reloadDataRequested = false;

    if (!m_resourcesDataInitialized) return;

    bool success = m_verovioToolkit.LoadData(m_fileContent.toStdString());
    setHasValidData(success);

    if (success) {
        requestDocumentRelayout();
    }
    else {
        setPageCount(0);
    }
}

void Toolkit::documentRelayout()
{
    m_documentRelayoutRequested = false;

    if (!m_hasValidData) {
        return;
    }

    if (!initFont()) {
        qWarning() << "Could not layout document because fonts are not correctly initialized";
        return;
    }

    m_verovioToolkit.SetPageWidth(static_cast<int>(m_displayWidth * 100.0 / m_verovioToolkit.GetScale()));
    m_verovioToolkit.SetPageHeight(static_cast<int>(m_displayHeight * 100.0 / m_verovioToolkit.GetScale()));

    m_verovioToolkit.RedoLayout();

    setPageCount(m_verovioToolkit.GetPageCount());

    emit documentLayoutChanged();
}
} // namespace vrvQt
