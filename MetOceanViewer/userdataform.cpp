#include "userdataform.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <array>

#include "colors.h"
#include "fileinfo.h"
#include "projection.h"

constexpr std::array<QColor, 4> s_colorPresets{
    QColor(0, 0, 255), QColor(0, 255, 0), QColor(255, 0, 0),
    QColor(255, 20, 147)};

UserdataForm::UserdataForm(QWidget *parent)
    : QDialog(parent),
      m_filename(new TextEntry<QString>("Filename:", "", 150, 200, this)),
      m_label(new TextEntry<QString>("Label:", "Series", 150, 200, this)),
      m_unitConversion(
          new TextEntry<double>("Unit Conversion:", 1.0, 150, 200, this)),
      m_timeAdjustment(
          new TextEntry<double>("Time Adjustment:", 0.0, 150, 200, this)),
      m_yAdjustment(
          new TextEntry<double>("y-adjustment:", 0.0, 150, 200, this)),
      m_coordinateSystem(
          new TextEntry<int>("Coordinate System:", 4326, 150, 200, this)),
      m_fileType(new TextEntry<QString>("File type:", "", 150, 200, this)),
      m_colorSelectButton(new QPushButton(this)),
      m_colorShortcut1(new QPushButton(this)),
      m_colorShortcut2(new QPushButton(this)),
      m_colorShortcut3(new QPushButton(this)),
      m_colorShortcut4(new QPushButton(this)),
      m_colorLabel(new QLabel("Color:", this)),
      m_coordinateSystemDescription(new QPushButton("Describe", this)),
      m_timeUnits(new QComboBox(this)),
      m_unitMenu(new UnitsMenu(this->m_unitConversion->box())),
      m_linestyle(new ComboBox("Select Line Style:", this)),
      m_browseFile(new QPushButton("Browse", this)),
      m_unitMenuButton(new UnitsMenu(this->m_unitConversion->box(), this)),
      m_buttons(new QDialogButtonBox(
          QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this)) {
  this->setMinimumWidth(600);
  this->setMaximumWidth(600);
  this->setMinimumHeight(600);
  this->setMaximumHeight(600);
  this->setWindowTitle("Add/Edit User Data Input");

  this->m_defaultBoxStylesheet = this->m_coordinateSystem->box()->styleSheet();

  this->setLayout(new QVBoxLayout());

  this->m_coordinateSystem->box()->setRange(0, std::numeric_limits<int>::max());
  this->m_coordinateSystem->box()->setValue(4326);

  this->m_linestyle->combo()->addItems(QStringList() << "Solid"
                                                     << "Dash"
                                                     << "Dash Dot"
                                                     << "Dash Dot Dot");

  this->m_timeUnits->addItems(QStringList() << "days"
                                            << "hours"
                                            << "minutes"
                                            << "seconds");
  this->m_timeUnits->setCurrentText("hours");

  this->m_label->layout()->setAlignment(Qt::AlignLeft);
  this->m_fileType->layout()->setAlignment(Qt::AlignLeft);

  QHBoxLayout *file = this->makeFileLayout();
  QHBoxLayout *unit = this->makeUnitLayout();
  QHBoxLayout *time = this->makeTimeLayout();
  QHBoxLayout *ys = this->makeYshiftLayout();
  QHBoxLayout *color = this->makeColorLayout();
  QHBoxLayout *cs = this->makeCoordinateLayout();
  QHBoxLayout *ls = this->makeLinestyleLayout();

  this->layout()->addItem(file);
  this->layout()->addWidget(this->m_label);
  this->layout()->addItem(unit);
  this->layout()->addItem(time);
  this->layout()->addItem(ys);
  this->layout()->addItem(color);
  this->layout()->addItem(ls);
  this->layout()->addItem(cs);
  this->layout()->addWidget(this->m_fileType);
  this->layout()->addItem(
      new QSpacerItem(0, 0, QSizePolicy::Preferred, QSizePolicy::Expanding));
  this->layout()->addWidget(this->m_buttons);
  this->layout()->setSpacing(0);
  static_cast<QVBoxLayout *>(this->layout())->addStretch();

  this->m_filename->box()->setReadOnly(true);
  this->m_fileType->box()->setReadOnly(true);
  this->m_unitConversion->box()->setDecimals(16);

  connect(this->m_buttons, SIGNAL(accepted()), this, SLOT(accept()));
  connect(this->m_buttons, SIGNAL(rejected()), this, SLOT(reject()));
  connect(this->m_colorSelectButton, SIGNAL(clicked()), this,
          SLOT(selectColor()));
  connect(m_colorShortcut1, &QPushButton::clicked,
          [=] { this->changeButtonColor(s_colorPresets[0]); });
  connect(m_colorShortcut2, &QPushButton::clicked,
          [=] { this->changeButtonColor(s_colorPresets[1]); });
  connect(m_colorShortcut3, &QPushButton::clicked,
          [=] { this->changeButtonColor(s_colorPresets[2]); });
  connect(m_colorShortcut4, &QPushButton::clicked,
          [=] { this->changeButtonColor(s_colorPresets[3]); });
  connect(m_coordinateSystem->box(), SIGNAL(valueChanged(int)), this,
          SLOT(checkEpsgCode(int)));
  connect(m_coordinateSystemDescription, SIGNAL(clicked()), this,
          SLOT(showEpsgDescription()));
  connect(m_browseFile, SIGNAL(clicked()), this, SLOT(browseFile()));

  this->getFormData();
}

void UserdataForm::setSeriesData(const UserdataSeries &data) {
  this->m_filename->box()->setText(FileInfo::basename(data.filename()));
  this->m_label->box()->setText(data.seriesName());
  this->m_unitConversion->box()->setValue(data.unitConversion());
  this->m_timeAdjustment->box()->setValue(data.xshift());
  this->m_timeUnits->setCurrentIndex(timeUnitToIndex(data.timeUnits()));
  this->m_yAdjustment->box()->setValue(data.yshift());
  this->m_selectedColor = data.color();
  Colors::changeButtonColor(this->m_colorSelectButton, this->m_selectedColor);
  this->m_coordinateSystem->box()->setValue(data.epsg());
}

QHBoxLayout *UserdataForm::makeFileLayout() {
  QHBoxLayout *file = new QHBoxLayout();
  file->addWidget(this->m_filename);
  file->addWidget(this->m_browseFile);
  file->addSpacerItem(
      new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Preferred));
  file->setAlignment(Qt::AlignLeft);
  return file;
}

QHBoxLayout *UserdataForm::makeUnitLayout() {
  QHBoxLayout *unit = new QHBoxLayout();
  unit->addWidget(this->m_unitConversion);
  unit->addWidget(this->m_unitMenuButton);
  unit->addSpacerItem(
      new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Preferred));
  unit->setAlignment(Qt::AlignLeft);
  return unit;
}

QHBoxLayout *UserdataForm::makeTimeLayout() {
  QHBoxLayout *time = new QHBoxLayout();
  time->addWidget(this->m_timeAdjustment);
  time->addWidget(this->m_timeUnits);
  time->addSpacerItem(
      new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Preferred));
  time->setAlignment(Qt::AlignLeft);
  return time;
}

QHBoxLayout *UserdataForm::makeYshiftLayout() {
  QHBoxLayout *ys = new QHBoxLayout();
  ys->addWidget(this->m_yAdjustment);
  ys->addSpacerItem(
      new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Preferred));
  ys->setAlignment(Qt::AlignLeft);
  return ys;
}

QHBoxLayout *UserdataForm::makeColorLayout() {
  QHBoxLayout *color = new QHBoxLayout();

  this->m_colorLabel->setMinimumWidth(163);
  this->m_colorLabel->setMaximumWidth(163);
  this->m_colorLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

  this->m_colorSelectButton->setMinimumWidth(200);
  this->m_colorSelectButton->setMaximumWidth(200);
  this->m_colorShortcut1->setMinimumWidth(30);
  this->m_colorShortcut2->setMinimumWidth(30);
  this->m_colorShortcut3->setMinimumWidth(30);
  this->m_colorShortcut4->setMinimumWidth(30);
  this->m_colorShortcut1->setMaximumWidth(30);
  this->m_colorShortcut2->setMaximumWidth(30);
  this->m_colorShortcut3->setMaximumWidth(30);
  this->m_colorShortcut4->setMaximumWidth(30);
  this->m_colorSelectButton->setMinimumHeight(30);
  this->m_colorShortcut1->setMinimumHeight(30);
  this->m_colorShortcut2->setMinimumHeight(30);
  this->m_colorShortcut3->setMinimumHeight(30);
  this->m_colorShortcut4->setMinimumHeight(30);
  this->m_colorShortcut1->setMaximumHeight(30);
  this->m_colorShortcut2->setMaximumHeight(30);
  this->m_colorShortcut3->setMaximumHeight(30);
  this->m_colorShortcut4->setMaximumHeight(30);

  this->m_colorSelectButton->setContentsMargins(5, 0, 5, 0);

  color->addWidget(this->m_colorLabel);
  color->addSpacerItem(
      new QSpacerItem(17, 0, QSizePolicy::Fixed, QSizePolicy::Preferred));
  color->addWidget(this->m_colorSelectButton);
  color->addSpacerItem(
      new QSpacerItem(15, 0, QSizePolicy::Fixed, QSizePolicy::Preferred));
  color->addWidget(this->m_colorShortcut1);
  color->addSpacerItem(
      new QSpacerItem(15, 0, QSizePolicy::Fixed, QSizePolicy::Preferred));
  color->addWidget(this->m_colorShortcut2);
  color->addSpacerItem(
      new QSpacerItem(15, 0, QSizePolicy::Fixed, QSizePolicy::Preferred));
  color->addWidget(this->m_colorShortcut3);
  color->addSpacerItem(
      new QSpacerItem(15, 0, QSizePolicy::Fixed, QSizePolicy::Preferred));
  color->addWidget(this->m_colorShortcut4);
  color->addSpacerItem(
      new QSpacerItem(15, 0, QSizePolicy::Expanding, QSizePolicy::Preferred));

  this->m_selectedColor = Colors::generateRandomColor();
  Colors::changeButtonColor(this->m_colorSelectButton, this->m_selectedColor);
  Colors::changeButtonColor(this->m_colorShortcut1, s_colorPresets[0]);
  Colors::changeButtonColor(this->m_colorShortcut2, s_colorPresets[1]);
  Colors::changeButtonColor(this->m_colorShortcut3, s_colorPresets[2]);
  Colors::changeButtonColor(this->m_colorShortcut4, s_colorPresets[3]);

  return color;
}

QHBoxLayout *UserdataForm::makeCoordinateLayout() {
  QHBoxLayout *cs = new QHBoxLayout();
  cs->addWidget(this->m_coordinateSystem);
  cs->addWidget(this->m_coordinateSystemDescription);
  cs->addSpacerItem(
      new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Preferred));
  return cs;
}

QHBoxLayout *UserdataForm::makeLinestyleLayout() {
  QHBoxLayout *ls = new QHBoxLayout();
  this->m_linestyle->label()->setMinimumWidth(150);
  this->m_linestyle->label()->setMaximumWidth(150);
  this->m_linestyle->combo()->setMinimumWidth(200);
  this->m_linestyle->combo()->setMaximumWidth(200);
  ls->addWidget(this->m_linestyle);
  ls->addItem(
      new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Preferred));
  return ls;
}

void UserdataForm::selectColor() {
  this->m_selectedColor = Colors::selectButtonColor(this->m_colorSelectButton);
}

void UserdataForm::changeButtonColor(const QColor c) {
  this->m_selectedColor = c;
  Colors::changeButtonColor(this->m_colorSelectButton, c);
}

int UserdataForm::timeUnitToIndex(const double t) {
  if (t == 86400.0)
    return 0;
  else if (t == 3600.0)
    return 1;
  else if (t == 60.0)
    return 2;
  else
    return 3;
}

double UserdataForm::timeUnitConversion(const int index) {
  switch (index) {
    case 0:return 86400.0;
    case 1:return 3600.0;
    case 2:return 60.0;
    case 3:return 1.0;
    default:return 1.0;
  }
}

void UserdataForm::getFormData() {
  this->m_series.setSeriesName(this->m_label->box()->text());
  this->m_series.setUnitConversion(this->m_unitConversion->box()->value());
  this->m_series.setXshift(this->m_timeAdjustment->box()->value());
  this->m_series.setYshift(this->m_yAdjustment->box()->value());
  this->m_series.setTimeUnits(
      this->timeUnitConversion(this->m_timeUnits->currentIndex()));
  this->m_series.setLinestyle(this->m_linestyle->combo()->currentIndex());
  this->m_series.setEpsg(this->m_coordinateSystem->box()->value());
  this->m_series.setColor(this->m_selectedColor);
  return;
}

void UserdataForm::checkEpsgCode(int epsg) {
  if (Hmdf::Projection::containsEpsg(epsg)) {
    this->m_coordinateSystem->box()->setStyleSheet(
        this->m_defaultBoxStylesheet);
  } else {
    this->m_coordinateSystem->box()->setStyleSheet(
        "background-color: rgb(255, 0, 0);");
  }
}

void UserdataForm::showEpsgDescription() {
  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Coordinate System Description");
  msgBox.setTextFormat(Qt::RichText);
  QString description;

  int epsg = this->m_coordinateSystem->box()->value();

  if (Hmdf::Projection::containsEpsg(epsg)) {
    int proj4code = this->m_coordinateSystem->box()->value();
    QString csDescription = QString::fromStdString(Hmdf::Projection::epsgDescription(epsg));
    description =
        QStringLiteral("<b>Coordinate System Reference:</b> <a "
                       "href=\"http://spatialreference.org/ref/epsg/") +
            QString::number(proj4code) +
            QStringLiteral("/\">SpatialReference.org</a>") +
            QStringLiteral("<br><b>Coordinate System Description:</b> ") +
            csDescription;
  } else {
    description = "<b>Error:</b> Invalid EPSG";
  }
  msgBox.setText(description);
  msgBox.setModal(true);
  msgBox.exec();
  msgBox.deleteLater();
  return;
}

void UserdataForm::browseFile() {
  QString path = QFileDialog::getOpenFileName(
      this, tr("Select File"), FileInfo::lastDirectory(),
      "MetOceanViewer Compatible file (*.imeds *.61 *.62 *.71 *.72 *.nc) ;; "
      "IMEDS File (*.imeds *.IMEDS) ;; netCDF Output Files (*.nc) ;; "
      "DFlow-FM History Files (*_his.nc) ;; "
      "ADCIRC Output Files (*.61 *.62 *.71 *.72) ;; All Files (*.*)");

  if (path.isEmpty())
    return;

  this->m_series.setFilename(path);
  this->m_filename->box()->setText(FileInfo::basename(path));
  FileInfo::changeLastDirectory(path);

  return;
}

UserdataSeries UserdataForm::series() {
  this->getFormData();
  return this->m_series;
}
