#ifndef MYQWEBENGINEPAGE_H
#define MYQWEBENGINEPAGE_H

#include <QWebEnginePage>
#include <QDesktopServices>

class MyQWebEnginePage : public QWebEnginePage
{
    Q_OBJECT

public:
    MyQWebEnginePage(QObject* parent = 0) : QWebEnginePage(parent){}

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
