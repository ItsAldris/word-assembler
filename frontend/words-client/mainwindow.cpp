#include "mainwindow.h"
#include "./ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // here add connect ui obj to fun. example:
    // connect(ui->conectBtn, &QPushButton::clicked, this, &MyWidget::connectBtnHit);

    connect(ui->connectButton, &QPushButton::clicked, this, &MainWindow::connectBtnHit);
    connect(ui->addressLineEdit, &QLineEdit::returnPressed, this, &MainWindow::connectBtnHit);

    connect(ui->acceptButton, &QPushButton::clicked, this, &MainWindow::sendBtnHit);
    connect(ui->usernameLineEdit, &QLineEdit::returnPressed, this, &MainWindow::sendBtnHit);

    connect(ui->disconnectButton, &QPushButton::clicked, this, &MainWindow::disconnectBtnHit);
}

MainWindow::~MainWindow()
{
    // if(sock)
    //    sock->close();
    delete ui;
}

void MainWindow::connectBtnHit(){
    ui->plainTextEdit->appendPlainText("connecting...");

    ui->plainTextEdit->appendPlainText("connected successfully");
}

void MainWindow::sendBtnHit(){
    ui->plainTextEdit->appendPlainText("waiting for server response...");

    ui->plainTextEdit->appendPlainText("username accepted");
    ui->stackedWidget->setCurrentIndex(1);
}

void MainWindow::disconnectBtnHit(){
    ui->plainTextEdit->appendPlainText("disconnected");
    ui->stackedWidget->setCurrentIndex(0);
}
