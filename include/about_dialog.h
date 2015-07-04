//----GPL-----------------------------------------------------------------------
//
// This file is part of MetOceanViewer.
//
//    MetOceanViewer is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    MetOceanViewer is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with MetOceanViewer.  If not, see <http://www.gnu.org/licenses/>.
//------------------------------------------------------------------------------

#ifndef ABOUT_DIALOG_H
#define ABOUT_DIALOG_H

#include <QDialog>

namespace Ui {
class about_dialog;
}

class about_dialog : public QDialog
{
    Q_OBJECT

public:
    explicit about_dialog(QWidget *parent = 0);
    ~about_dialog();

private slots:
    void on_button_ok_clicked();

private:
    Ui::about_dialog *ui;
    QString generateAboutText();
};

#endif // ABOUT_DIALOG_H
