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
