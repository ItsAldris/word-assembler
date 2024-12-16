#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QLCDNumber>
#include <QStackedWidget>
#include <QStatusBar>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow *ui;

    // 1 strona
    QLineEdit *addressLineEdit;
    QSpinBox *portSpinBox;
    QPushButton *connectButton;
    QPlainTextEdit *plainTextEdit;
    QLineEdit *usernameLineEdit;
    QPushButton *acceptButton;

    // 2 strona
    QLineEdit *lettersLineEdit;
    QLCDNumber *lcdNumber;
    QPushButton *disconnectButton;
    QPlainTextEdit *plainTextEdit_2;
    QPlainTextEdit *statsPlainTextEdit;
    QLineEdit *wordLineEdit;
    QPushButton *acceptButton_1;

    // odpowiedzialny za zmiane strony
    QStackedWidget *stackedWidget;

    // pomocnicze
    void switchToPage(int pageIndex);
    void connectBtnHit();
    void sendBtnHit();
    void disconnectBtnHit();
};
#endif // MAINWINDOW_H
