#ifndef MYQWEBENGINEPAGE_H
#define MYQWEBENGINEPAGE_H

#include <QWebEngineView>
#include <QWebEnginePage>
#include <QDesktopServices>
#include <QtCharts>

using namespace QtCharts;

class mov_QWebEnginePage : public QWebEnginePage
{
    Q_OBJECT

public:
    mov_QWebEnginePage(QObject* parent = 0) : QWebEnginePage(parent){}

    bool acceptNavigationRequest(const QUrl & url, QWebEnginePage::NavigationType type, bool isMainFrame)
    {
        if (type == QWebEnginePage::NavigationTypeLinkClicked)
        {
            QDesktopServices::openUrl(url);
            return false;
        }
        return true;
    }
};

#endif // MYQWEBENGINEPAGE_H
