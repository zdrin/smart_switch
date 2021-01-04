#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <sys/time.h>
#include <time.h>
#include "FS.h"
#include "String.h"
#include <TZ.h>




#define RTC_TEST 1499893466 // = Wed 12 July 2017 21:04:26 UTC
#define MYTZ TZ_Asia_Shanghai

String wifi_mode, ssid, password, http_api;//需要配置或指定的全局参数
int Pin = 13; // 向继电器输出电平的针脚  GPIO13---D7 of NodeMCU
int Pin_value = LOW;//开机默认为低电平，继电器断开
int lc = 0;
timeval tv = { RTC_TEST, 0 };
int start_time = 0;//用于存储基于时间开启电源的开始时间，隔20分钟检测系统是否启动成功，若没有成功系统重新启动
int stop_time = 0;//用于存储获取到关闭电源信号的时间，获取到关闭电源信号后，延迟3分钟关闭电源
ESP8266WebServer server(80);

//基于本地SERVER接收外来触发信号  开open（通路） 关close（断路）  标准访问格式：http://192.168.1.1/act?status=open 成功返回suc 错误返回err
void handleAct(){
  String status = server.arg("status");
  if(status == "open"){
    digitalWrite(Pin, HIGH);//高电平触发信号下发
    Pin_value = HIGH;//同步信号值
  }
  else if(status == "close"){
    digitalWrite(Pin, LOW);
    Pin_value = LOW;
  }
  else{
    server.send(200, "text/html", "err");
    return;
  }
  Serial.println(status);
  server.send(200, "text/html", "suc");
}
//登录访问配置页面反馈,并且装载config_data中的参数到页面中
void handleRoot() {
  String response = "<html><head><meta charset='utf-8'><meta name='viewport'content='width=device-width, initial-scale=1.0'><title>可编程智能开关</title><style type='text/css'>*{margin:0;padding:0}body{background-color:#1a9ee6;font-family:Microsoft Yahei}form{margin:5%10%5%10%;width:80%}p{width:100%;text-align:center;color:#575757}input[type=text]{width:100%;height:2em;margin:0 0 2em 0;border-radius:.5em}input[type=submit]{margin:0 30%;width:40%;cursor:pointer;text-align:center;text-decoration:none;font-size:1em;padding:.5em 2em.55em;text-shadow:0 1px 1px rgba(0,0,0,.3);border-radius:.5em;-webkit-box-shadow:0 1px 2px rgba(0,0,0,.2);-moz-box-shadow:0 1px 2px rgba(0,0,0,.2);box-shadow:0 1px 2px rgba(0,0,0,.2);color:#fef4e9;border:solid 1px#da7c0c;background:#f78d1d;background:-webkit-gradient(linear,left top,left bottom,from(#faa51a),to(#f47a20));background:-moz-linear-gradient(top,#faa51a,#f47a20)}</style></head><body><form action='/cfg'method='post'><p>要接入的WiFi名称</p><input type='text'name='ssid'value='";
  response.concat(ssid);
  response.concat("'><p>WiFi密码</p><input type='text'name='password'value='");
  response.concat(password);
  response.concat("'/><p>远程web访问接口</p><input type='text'name='http_api'value='");
  response.concat(http_api);
  response.concat("'/><input type='submit'value='刷新配置'></form></body></html>");
  server.send(200, "text/html", response);
}
//根据client提交的数据写配置参数config.json
void handleCfg() {
  if(server.arg("ssid") == "" || server.arg("password") == ""){
    server.send(200, "text/html", "<html><head><meta charset='utf-8'><meta name='viewport'content='width=device-width, initial-scale=1.0'><title>可编程智能开关</title><style type='text/css'>*{margin:0;padding:0}body{background-color:#1a9ee6;font-family:Microsoft Yahei}</style><script type='text/javascript'>alert('配置参数失败，请重试');</script></head><body></body></html>");
    return;
  }
  StaticJsonDocument<200> config_update;
  //开始接收参数并改当前参数
  config_update["wifi_mode"] = "WIFI_STA";//修改为客户端模式
  ssid = server.arg("ssid");
  config_update["ssid"] = ssid;
  password = server.arg("password");
  config_update["password"] = password;
  http_api = server.arg("http_api");
  config_update["http_api"] = http_api;
  File configFile = SPIFFS.open("/config.json", "w");
  serializeJson(config_update, configFile);//将新的配置参数写入flash
  server.send(200, "text/html", "<html><head><meta charset='utf-8'><meta name='viewport'content='width=device-width, initial-scale=1.0'><title>可编程智能开关</title><style type='text/css'>*{margin:0;padding:0}body{background-color:#1a9ee6;font-family:Microsoft Yahei}</style><script type='text/javascript'>alert('配置参数成功');</script></head><body></body></html>");
  delay(2000);
  if(wifi_mode == "WIFI_AP"){//首次配置完成
    wifi_mode = "WIFI_STA";
    WiFi.softAPdisconnect();//关闭AP
    WiFi.mode(WIFI_STA);//开启用户模式
  }
  else if(wifi_mode == "WIFI_STA"){//修改配置完成
    WiFi.disconnect();//断开连接
  }
  WiFi.begin(ssid, password);//连接WiFi
  delay(10);//不知为何，直接wifi.status总是不能开启AP
  int ConnectCount = 0;
  while (WiFi.status() != WL_CONNECTED) {
    if(ConnectCount >= 60){//30秒之内连接失败则恢复AP模式，重新设置用户名密码
      StaticJsonDocument<200> config_change;
      wifi_mode = "WIFI_AP";
      config_change["wifi_mode"] = wifi_mode;//恢复为AP模式
      ssid.concat("_err");//出错后，参数加err前缀
      config_change["ssid"] = ssid;
      password.concat("_err");
      config_change["password"] = password;
      http_api.concat("_err");
      config_change["http_api"] = http_api;
      configFile = SPIFFS.open("/config.json", "w");
      serializeJson(config_change, configFile);//将新的配置参数写入flash
      WiFi.disconnect();//断开连接
      WiFi.softAP("smart_switch");//切换为AP模式
      IPAddress local(192,168,1,1);
      IPAddress gateway(192,168,1,1);
      IPAddress subnet(255,255,255,0);
      WiFi.softAPConfig(local,gateway,subnet);//设置ip,网关,掩码
      break;
    }
    delay(500);
    ConnectCount = ConnectCount + 1;
  } 


  Serial.println(WiFi.localIP());
  Serial.println(WiFi.macAddress());
  
}


void setup() {
  Serial.begin(115200);//开启串口
  SPIFFS.begin();//开启文件系统
  delay(1000);
  /*读取参数文件并写入参数*/
  File configFile = SPIFFS.open("/config.json", "r");
  size_t size = configFile.size();
  std::unique_ptr<char[]> buf(new char[size]);
  configFile.readBytes(buf.get(), size);
  StaticJsonDocument<200> config_data;
  deserializeJson(config_data, buf.get());//读取参数数组
  //开始写入全局参数,必须像下面这样写，要不然出错;config_data[]只能赋值给const char*
  const char* wifi_mode_t = config_data["wifi_mode"];
  wifi_mode = wifi_mode_t;
  const char* ssid_t = config_data["ssid"];
  ssid = ssid_t;
  const char* password_t = config_data["password"];
  password = password_t;
  const char* http_api_t = config_data["http_api"];
  http_api = http_api_t;


  Serial.println(wifi_mode);
  Serial.println(ssid);
  Serial.println(password);
  Serial.println(http_api);

  
  /*--读取参数文件并写入参数数组--*/
  if(wifi_mode == "WIFI_AP"){
    WiFi.softAP("smart_switch");
    IPAddress local(192,168,1,1);
    IPAddress gateway(192,168,1,1);
    IPAddress subnet(255,255,255,0);
    WiFi.softAPConfig(local,gateway,subnet);//设置ip,网关,掩码
    Serial.println("wifi_ap");
  }
  else if(wifi_mode == "WIFI_STA"){
    WiFi.mode(WIFI_STA);//开启用户模式
    WiFi.begin(ssid, password);//连接WiFi
    int ConnectCount = 0;
    while (WiFi.status() != WL_CONNECTED) {
      if(ConnectCount >= 60){//30秒之内连接失败则恢复AP模式，重新设置用户名密码
        StaticJsonDocument<200> config_update;
        wifi_mode = "WIFI_AP";
        config_update["wifi_mode"] = wifi_mode;//恢复为AP模式
        ssid.concat("_err");//出错后，参数加err前缀
        config_update["ssid"] = ssid;
        password.concat("_err");
        config_update["password"] = password;
        http_api.concat("_err");
        config_update["http_api"] = http_api;
        configFile = SPIFFS.open("/config.json", "w");
        serializeJson(config_update, configFile);//将新的配置参数写入flash
        WiFi.disconnect();//断开连接
        WiFi.softAP("smart_switch");//切换为AP模式
        IPAddress local(192,168,1,1);
        IPAddress gateway(192,168,1,1);
        IPAddress subnet(255,255,255,0);
        WiFi.softAPConfig(local,gateway,subnet);//设置ip,网关,掩码
        break;
      }
      delay(500);
      ConnectCount = ConnectCount + 1;
    }


    Serial.println(WiFi.localIP());
    Serial.println(WiFi.macAddress());
    
  }
  server.on("/", handleRoot);//指定服务接口
  server.on("/cfg", handleCfg);//指定服务接口
  server.on("/act", handleAct);//指定服务接口
  server.begin();//开启服务
  pinMode(Pin, OUTPUT);//开启pin为output模式
  
  settimeofday(&tv, nullptr);
  configTime(MYTZ, "ntp1.aliyun.com", "ntp4.aliyun.com", "ntp7.aliyun.com");
  WiFi.persistent(false); // 不保存任何wifi配置到flash
}
void loop() {
  server.handleClient();
  /*基于时间开启电源*/
  char strftime_buf[64];
  time_t tnow = time(nullptr);
  struct tm *tmstruct;
  tmstruct = gmtime(&tnow);
  int week = tmstruct->tm_wday;//星期
  if(week > 0 && week < 6){//工作日开启
    int now_hour = tmstruct->tm_hour;//小时，范围从0到23
    int now_min = tmstruct->tm_min; //分，范围从0到59
    if(Pin_value == LOW && now_hour == 19 && now_min == 0){//电源处于关闭状态，且时间在19点，开启电源
      digitalWrite(Pin, HIGH);//高电平触发信号下发
      Pin_value = HIGH;//同步信号值
      start_time = millis();//记录开机时刻
    }
  }
  /*--基于时间开启电源--*/
  if(wifi_mode == "WIFI_STA" && http_api != ""){//用户模式轮询远程API
    WiFiClient client;
    HTTPClient http;
    String url = "http://";
    url.concat(http_api);//合成标准访问地址
    http.begin(client, url);//访问url
    int httpCode = http.GET();//返回访问状态
    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
      String response = http.getString();//远程API响应
      /*检查系统在开启电源后是否成功启动，要是启动失败重新启动*/
      if(start_time > 0 && millis() - start_time > 1200000){//20分钟后检测
        if(response == "wait"){//启动失败
          digitalWrite(Pin, LOW);//关闭
          delay(5000);//暂停5秒
          digitalWrite(Pin, HIGH);//上电
          start_time = millis();//更新上电时间
        }
        else if(response == "working"){//启动成功
          start_time = 0;//上电时间复位
        }
      }
      /*--检查系统在开启电源后是否成功启动，要是启动失败重新启动--*/
      /*延迟关闭电源动作*/
      if(stop_time > 0 && millis() - stop_time > 180000){//3分钟后关机
        digitalWrite(Pin, LOW);
        Pin_value = LOW;
        stop_time = 0;//完成关机动作,关机时间复位
      }
      /*--延迟关闭电源动作--*/
      
      Serial.println(response);
      
      if(response == "stop"){//关闭电源
        stop_time = millis();//记录获取到关机信号的时间,准备关机动作
        if(start_time > 0){//有可能提前关机，来不及在开机20分钟的时候返回working
          start_time = 0;
        }
        do{
          http.begin(client, "http://api_url");//完成断开电源任务，使远程数据库标识复位为wait
          httpCode = http.GET();//返回访问状态
          if(httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY){
            response = http.getString();
          }
        }
        while(response != "suc");
      }
      else if(response == "working" && Pin_value == LOW){//由外部手动给信号开启电源
        digitalWrite(Pin, HIGH);//高电平触发信号下发
        Pin_value = HIGH;//同步信号值
        start_time = millis();//记录开机时刻
      }
      Serial.println(response);
    }
    
    http.end();
    delay(3000);
  }
  
}
