#ifndef USERDATATAB_H
#define USERDATATAB_H

#include <QTabWidget>
#include <QTableView>
#include <QWidget>

#include "mapfunctions.h"
#include "tabtypes.h"
#include "userdatamap.h"
#include "userdatatable.h"

class UserdataTab : public QWidget {
 public:
  UserdataTab(QWidget *parent = nullptr);

  TabType type() const;

  void initialize();

 private:
  const TabType m_type;
  QVBoxLayout *m_windowLayout;
  QTabWidget *m_tabWidget;
  UserdataMap *m_mapChartWidget;
  UserdataTable *m_dataTableWidget;
};

#endif  // USERDATATAB_H
