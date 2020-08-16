#include "userdatatable.h"

#include <QDebug>
#include <QHeaderView>

#include "userdataform.h"

UserdataTable::UserdataTable(QWidget *parent)
    : QWidget(parent), m_tableWidget(new QTableWidget(this)) {
  this->setLayout(new QVBoxLayout());
  this->layout()->addWidget(this->m_tableWidget);
  this->setAutoFillBackground(true);

  this->m_tableWidget->setColumnCount(6);
  this->m_tableWidget->setRowCount(0);
  this->m_tableWidget->setHorizontalHeaderLabels(QStringList()
                                                 << "Filename"
                                                 << "Series Name"
                                                 << "Color"
                                                 << "Unit Conversion"
                                                 << "x-shift"
                                                 << "y-shift");
  this->m_tableWidget->horizontalHeader()->setSectionResizeMode(
      QHeaderView::Stretch);
  this->m_tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
  this->layout()->addItem(this->generateButtonBar());
  this->layout()->addItem(this->generateAxisOptions());
  this->layout()->addWidget(this->generateLabelOptions());
}

QHBoxLayout *UserdataTable::generateButtonBar() {
  this->m_addButton = new QPushButton(this);
  this->m_editButton = new QPushButton(this);
  this->m_copyButton = new QPushButton(this);
  this->m_deleteButton = new QPushButton(this);
  this->m_upButton = new QPushButton(this);
  this->m_downButton = new QPushButton(this);

  this->m_addButton->setText("Add");
  this->m_editButton->setText("Edit");
  this->m_copyButton->setText("Copy");
  this->m_deleteButton->setText("Delete");

  QPixmap upicon(":/rsc/img/arrow_up.png");
  QPixmap downicon(":/rsc/img/arrow_down.png");
  this->m_upButton->setIcon(QIcon(upicon));
  this->m_downButton->setIcon(QIcon(downicon));

  QHBoxLayout *layout = new QHBoxLayout();
  layout->addSpacerItem(
      new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Preferred));
  layout->addWidget(this->m_addButton);
  layout->addWidget(this->m_editButton);
  layout->addWidget(this->m_copyButton);
  layout->addWidget(this->m_deleteButton);
  layout->addWidget(this->m_upButton);
  layout->addWidget(this->m_downButton);
  layout->addStretch();
  layout->addSpacing(0);
  layout->setAlignment(Qt::AlignHCenter);

  connect(this->m_addButton, SIGNAL(clicked()), this, SLOT(onClickAddSeries()));
  connect(this->m_editButton, SIGNAL(clicked()), this,
          SLOT(onClickEditSeries()));
  connect(this->m_deleteButton, SIGNAL(clicked()), this,
          SLOT(onClickDeleteSeries()));
  connect(this->m_copyButton, SIGNAL(clicked()), this,
          SLOT(onClickCopySeries()));
  connect(this->m_upButton, SIGNAL(clicked()), this,
          SLOT(onClickMoveUpSeries()));
  connect(this->m_downButton, SIGNAL(clicked()), this,
          SLOT(onClickMoveDownSeries()));

  return layout;
}

QHBoxLayout *UserdataTable::generateAxisOptions() {
  QGroupBox *xaxisbox = new QGroupBox(this);
  QGroupBox *yaxisbox = new QGroupBox(this);

  xaxisbox->setTitle("Set Date Axis Range");
  yaxisbox->setTitle("Set y-Axis Range");

  this->m_dateCheck = new QCheckBox(this);
  this->m_yCheck = new QCheckBox(this);

  this->m_dateCheck->setText("Auto");
  this->m_yCheck->setText("Auto");
  this->m_dateCheck->setCheckState(Qt::Checked);
  this->m_yCheck->setCheckState(Qt::Checked);

  this->m_startDate = new QDateEdit(this);
  this->m_endDate = new QDateEdit(this);

  this->m_startDate->setDateTime(QDateTime(QDate(1980, 1, 1), QTime(0, 0, 0)));
  this->m_endDate->setDateTime(QDateTime(QDate(2050, 1, 1), QTime(0, 0, 0)));

  this->m_startDate->setEnabled(false);
  this->m_endDate->setEnabled(false);
  this->m_startDate->setMinimumWidth(150);
  this->m_endDate->setMinimumWidth(150);
  this->m_startDate->setDisplayFormat("MM/dd/yyyy");
  this->m_endDate->setDisplayFormat("MM/dd/yyyy");

  this->m_ymin = new QDoubleSpinBox(this);
  this->m_ymax = new QDoubleSpinBox(this);

  this->m_ymin->setEnabled(false);
  this->m_ymax->setEnabled(false);
  this->m_ymin->setMinimumWidth(100);
  this->m_ymax->setMinimumWidth(100);

  QHBoxLayout *xlayout = new QHBoxLayout();
  QHBoxLayout *ylayout = new QHBoxLayout();

  xlayout->addWidget(this->m_dateCheck);
  xlayout->addWidget(this->m_startDate);
  xlayout->addWidget(this->m_endDate);
  xlayout->addStretch();
  xlayout->addSpacing(0);

  ylayout->addWidget(this->m_yCheck);
  ylayout->addWidget(this->m_ymin);
  ylayout->addWidget(this->m_ymax);
  ylayout->addStretch();
  ylayout->addSpacing(0);

  xaxisbox->setLayout(xlayout);
  yaxisbox->setLayout(ylayout);

  QHBoxLayout *xylayout = new QHBoxLayout();

  xylayout->addSpacerItem(
      new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Preferred));
  xylayout->addWidget(xaxisbox);
  xylayout->addSpacerItem(
      new QSpacerItem(10, 0, QSizePolicy::Fixed, QSizePolicy::Fixed));
  xylayout->addWidget(yaxisbox);
  xylayout->addStretch();
  xylayout->setAlignment(Qt::AlignHCenter);

  connect(m_dateCheck, SIGNAL(toggled(bool)), this,
          SLOT(onCheckDateAxisAuto(bool)));
  connect(m_yCheck, SIGNAL(toggled(bool)), this, SLOT(onCheckYAxisAuto(bool)));

  return xylayout;
}

QGroupBox *UserdataTable::generateLabelOptions() {
  this->m_title = new TextEntry<QString>(
      "Main Plot Title:", "Timeseries Comparison", 0, 250, this);
  this->m_xlabel =
      new TextEntry<QString>("x-axis label:", "Date (GMT)", 0, 250, this);
  this->m_ylabel =
      new TextEntry<QString>("y-axis label:", "Water Surface Elevation (m)",
                             this->m_xlabel->label()->width(), 250, this);

  QVBoxLayout *vlayout = new QVBoxLayout();
  QHBoxLayout *hlayout = new QHBoxLayout();
  hlayout->addWidget(this->m_title);
  vlayout->addWidget(this->m_xlabel);
  vlayout->addWidget(this->m_ylabel);
  hlayout->addLayout(vlayout);
  hlayout->addSpacing(0);
  vlayout->addSpacing(0);
  vlayout->addSpacerItem(
      new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Preferred));
  hlayout->addSpacerItem(
      new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Preferred));

  QGroupBox *gb = new QGroupBox(this);
  gb->setLayout(hlayout);
  gb->setTitle("Plot Labels");
  gb->setMinimumHeight(gb->minimumSizeHint().height());
  gb->setMaximumHeight(gb->minimumSizeHint().height() + 10);
  gb->setContentsMargins(0, 0, 0, 0);

  return gb;
}

void UserdataTable::onCheckDateAxisAuto(bool checked) {
  this->m_startDate->setEnabled(!checked);
  this->m_endDate->setEnabled(!checked);
}

void UserdataTable::onCheckYAxisAuto(bool checked) {
  this->m_ymin->setEnabled(!checked);
  this->m_ymax->setEnabled(!checked);
}

void UserdataTable::onClickAddSeries() {
  UserdataForm *form = new UserdataForm(this);
  auto status = form->exec();
  if (status == QDialog::DialogCode::Accepted) {
    qDebug() << "HERE!";
  }
}

void UserdataTable::onClickCopySeries() {}

void UserdataTable::onClickEditSeries() {}

void UserdataTable::onClickDeleteSeries() {}

void UserdataTable::onClickMoveUpSeries() {}

void UserdataTable::onClickMoveDownSeries() {}
