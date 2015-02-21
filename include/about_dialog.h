//---- LGPL --------------------------------------------------------------------
//
// Copyright (C)  ARCADIS, 2011-2013.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation version 3.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// contact: Zachary Cobell, zachary.cobell@arcadis-us.com
// ARCADIS
// 4999 Pearl East Circle, Suite 200
// Boulder, CO 80301
//
// All indications and logos of, and references to, "ARCADIS"
// are registered trademarks of ARCADIS, and remain the property of
// ARCADIS. All rights reserved.
//
//------------------------------------------------------------------------------
// $Author$
// $Date$
// $Id$
// $HeadURL$
//------------------------------------------------------------------------------
//  File: about_dialog.h
//
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
