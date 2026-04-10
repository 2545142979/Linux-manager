#ifndef HOMEWIDGET_H
#define HOMEWIDGET_H

#include <QWidget>
#include <QTcpSocket>
#include <QTimer>
#include <QPixmap>
#include <QByteArray>


QT_BEGIN_NAMESPACE
namespace Ui { class HomeWidget; }
QT_END_NAMESPACE

class HomeWidget : public QWidget
{
    Q_OBJECT

public:
    HomeWidget(QWidget *parent = nullptr);
    ~HomeWidget();

private slots:
    // 设备控制
       void on_btn_light_clicked();
       void on_btn_buzzer_clicked();
       void on_btn_fan_clicked();

       // 网络
       void on_btn_connect_clicked();
       void on_btn_disconnect_clicked();
       void on_btn_monitor_clicked();
       void on_btn_env_clicked();

       // 图片接收逻辑
       void netConnected();
       void netDisConnected();
       void recvMsg();
       void requestPic();
       void requestEnv();
       void netError(QAbstractSocket::SocketError error);

       // 日志
       void log(const QString &msg);


private:
    void processIncomingBuffer();
    bool tryConsumeAckMessage();
    bool tryConsumeEnvPacket();
    bool tryConsumeImageLength();
    bool tryConsumeImagePayload();
    void applyEnvPacket(const QByteArray &packet);
    void sendTextCommand(const char *command, const QString &logMessage);

    Ui::HomeWidget *ui;
    QTcpSocket *socket;
       int status = 1;
       int pic_len = 0;
       QTimer *time;
       QTimer *envTimer;
       QByteArray recvBuffer;

       // 设备状态
       bool lightState = false;
       bool buzzerState = false;
       bool fanState = false;
       bool isMonitoring = false;
       bool envVisible = true;
};
#endif // HOMEWIDGET_H
