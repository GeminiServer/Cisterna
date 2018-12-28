#include <wiringPi.h>
#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <string>
#include <cstdarg>

#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include "pistache/serializer/rapidjson.h"
#include "pistache/http.h"
#include "pistache/description.h"
#include "pistache/endpoint.h"

//Info: Cisterne ist bei 100%, wenn wasserstand von der decke 16cm abstand hat.
// Die höhe der cisterne ist 223.00cm
// Durchmesser der Cisterne beträgt: 207.00cm

//|----------------------|
//|    (_) SENSOR (_)    |
//|----------------------|   --> Max. Höhe (fCisternaMaxHigh)  / Max. Volumen (fCisternaMaxVolume)
//||||||||||||||||||||||||   --> Überlauf Differenz (fSurfaceOffSet) Offset überlauf!
//|----------------------|  .--> Oberfläche / Überlaufhöhe / ÜberlaufVolumen / Ansaug START == 100%
//|----------------------|   -->
//|()()()()()()()()()()()|   }
//|()()()()()()()()()()()|   }
//|()()()()()()()()()()()|   } Nutzwasser: Höhe & Volumen
//|()()()()()()()()()()()|   }
//|()()()()()()()()()()()|   }
//|----------------------|  --> Sedimenthöhe (fSuctionHead)= Ansaug ENDE!
//||||||||||||||||||||||||
//|----------------------|  --> ZERO Cisterna


using namespace Pistache;
using namespace std;
using namespace rapidjson;

  /*
    Global declarations
  */
  string strVersion = "Cisterna PI - v0.1 BETA  (c) 2018 Erkan Colak";
  bool bDebug= false;
  bool bCalibration= false;           // Calibration mode. Minus values will be also measured! Use only with DebugMode!
                                      // Delay time in ms. Delay time before the next measurement starts
  uint uiDELAY_TIME_NEXT_MEASURE = 1000; // Delay time in ms. Delay time before the next measurement start. Default 50ms. May use 500ms or 1000ms
  uint uiMEASURE_VAL             =  100; // Count of measurements for calculating the median value
  uint uiMEASURE_TIMEOUT_IN_SEC  =    1; // Timeout in second. Time to wait for the response. Default is 1 Second (33130cm/s).
                                       // The object is not present or seems to be to far.

  // Ultrasonic Measurement Settings
  uint uiMEASURE_DELAYWRITELOW      =100; // Microseconds
  uint uiMEASURE_DELAYWRITEHIGH     =50 ; // Microseconds
  uint uiMEASURE_DELAYWRITELOWHIGH  =50 ; // Microseconds

  // Cisterna Informations. All Values are in cm!
  float fSurfaceOffSet      = 0;         // Offset from UltraSonic Sensor to the Surface
  float fInternalOffSet     = -2.00;      // Sonic Sensor
  float fCisternaHighMax    = 223.00;     // Maximale höhe ist 223.00cm
  float fCisternaRadius     = 103.50;     // Durchmesser 207.00cm
  float fSuctionHead        = 20.00;      // Sedimenthöhe: vom Boden - ca. 10 cm Sediment ca. 10cm saughöhe

  // Raspberry PI Setting
  int TRIGGER = 4;                      // GPIO:23 - WiringPI:4
  int ECHO    = 5;                      // GPIO:24 - WiringPi:5

  struct timespec ts_res, ts_start, ts_end;
  struct distance_history_t {
    float fA;
    float fB;
    float fC;
  };

  struct cis_volume_info_t {
    float fInPercent;
    float fCurrentLiter;
    float fMinLiter;
    float fMaxLiter;
  };

  struct measurement_info_t {
    bool bValidInfo;
    string strDate;
    float fDistance;
    float fDistanceLive;
    int iMeasurements;
    bool bWaterSuctionRelease;
    distance_history_t Distance_History;
    cis_volume_info_t CistVolInfo;
  };


  // Global message
  measurement_info_t Info_Msg_t;
  string strBinPath, strBinName;

  /*
    Function:
    Description:
  */
  float CalcMedian(float *a) {
    float left[uiMEASURE_VAL /2], right[uiMEASURE_VAL /2], median, *p;
    unsigned char nLeft, nRight;
    p = a; median = *p++; nLeft = nRight = 1;                            // pick first value as median candidate

    for (;;) {
      float val = *p++;                                                  // get next value
      if (val < median) {                                                // if value is smaller than median, append to left heap
        unsigned char child = nLeft++, parent = (child - 1) / 2;         // move biggest value to the heap top
        while (parent && val > left[parent]) {
          left[child] = left[parent];
          child = parent;
          parent = (parent - 1) / 2;
        } left[child] = val;

        if (nLeft == 14) {                                               // if left heap is full
          for (unsigned char nVal = 27 - (p - a); nVal; --nVal) {        // for each remaining value
            val = *p++;                                                  // get next value
            if (val < median) {                                          // if value is to be inserted in the left heap
              child = left[2] > left[1] ? 2 : 1;
              if (val >= left[child]) median = val;
              else {
                median = left[child];
                parent = child;
                child = parent * 2 + 1;
                while (child < 14) {
                  if (child < 13 && left[child + 1] > left[child]) ++child;
                  if (val >= left[child]) break;
                  left[parent] = left[child];
                  parent = child;
                  child = parent * 2 + 1;
                }
                left[parent] = val;
              }
            }
          }
          return median;
        }
      } else {                                                            // else append to right heap
        unsigned char child = nRight++, parent = (child - 1) / 2;         // move smallest value to the heap top
        while (parent && val < right[parent]) {
          right[child] = right[parent];
          child = parent;
          parent = (parent - 1) / 2;
        } right[child] = val;

        if (nRight == 14) {                                               // if right heap is full
          for (unsigned char nVal = 27 - (p - a); nVal; --nVal) {         // for each remaining value
            val = *p++;                                                   // get next value
            if (val > median) {                                           // if value is to be inserted in the right heap
              child = right[2] < right[1] ? 2 : 1;
              if (val <= right[child]) median = val;
              else {
                median = right[child];
                parent = child;
                child = parent * 2 + 1;
                while (child < 14) {
                  if (child < 13 && right[child + 1] < right[child]) ++child;
                  if (val <= right[child]) break;
                  right[parent] = right[child];
                  parent = child;
                  child = parent * 2 + 1;
                }
                right[parent] = val;
              }
            }
          }
          return median;
        }
      }
    }
  }

  /*
    Function:
    Description:
  */
  inline string DoStrFormat(const char* fmt, ...){
    int size = 512;
    char* buffer = 0;
    buffer = new char[size];
    va_list vl;
    va_start(vl, fmt);
    int nsize = vsnprintf(buffer, size, fmt, vl);
    if(size<=nsize) { //fail delete buffer and try again
        delete[] buffer;
        buffer = 0;
        buffer = new char[nsize+1]; //+1 for /0
        nsize = vsnprintf(buffer, size, fmt, vl);
    }
    string ret(buffer);
    va_end(vl);
    delete[] buffer;
    return ret;
  }

  /*
    Function:
    Description:
  */
  bool getBinPath() {
    bool bRet= false;
    char arg1[20], exepath[PATH_MAX + 1] = {0};
    sprintf( arg1, "/proc/%d/exe", getpid() );
    if( bRet= (readlink( arg1, exepath, 1024 ) > -1) ) {
      string strTmp;strTmp.assign(exepath);
      strBinPath= strTmp.substr(0,strTmp.find_last_of("/\\"));
      string strBinName= strTmp.substr(strTmp.find_last_of("/\\")+1);
      if(bDebug) printf("\nBinName: %s\nBinPath: %s\n\n",strBinName.c_str(), strBinPath.c_str());
    }
    return bRet;
  }

  /*
    Function:
    Description:
  */
  string TimeStr() {
    time_t t; time(&t);
    struct tm *ptm = gmtime ( &t );
    string strRet; char cTime[1024];
    sprintf( cTime, "%02d.%02d.%02d - %2d:%02d:%02d",
                    ptm->tm_mday, ptm->tm_mon, ptm->tm_year+1900,
                    (ptm->tm_hour+2/*UTC+2*/)%24, ptm->tm_min, ptm->tm_sec
           );
    strRet.assign(cTime);
    return strRet;
  }

  /*
    Function: Starttimer
    Description: Get the start timer (it is the initial start time )
  */
  void Starttime (void) {
    clock_gettime(CLOCK_MONOTONIC, &ts_start);
  }

  /*
    Function: EndTime()
    Description: Get the EndTimer
  */
  void Endtime (void) {
    clock_gettime(CLOCK_MONOTONIC, &ts_end);
  }

  /*
    Funtions: InitWiringPi
    Description:
  */
  bool InitWiringPi() {
    bool bRet= !(wiringPiSetup () == -1);
    pinMode (ECHO, INPUT) ;
    pinMode (TRIGGER, OUTPUT);
    if( bDebug ) printf(bRet ? "WiringPi Setup successful!\n" : "WiringPi Setup failed. Please check the wiring!");
    return bRet;
  }

  /*
    Funtions: CalcDIstance
    Description: Will calculata the distance to the supersonic compatible object
    Return: Distance in Nanometer!
  */
  float CalcDistance () {
    time_t endwait;

    clock_getres(CLOCK_MONOTONIC, &ts_res);
    digitalWrite(TRIGGER,LOW);
    delayMicroseconds(uiMEASURE_DELAYWRITELOW);                   // sleep(0.5); // defautl is 0.5!

    digitalWrite(TRIGGER, HIGH);
    delayMicroseconds (uiMEASURE_DELAYWRITEHIGH);                 // default is 10

    digitalWrite(TRIGGER,LOW);
    delayMicroseconds (uiMEASURE_DELAYWRITELOWHIGH);              // default is 10

    endwait = time (NULL) + uiMEASURE_TIMEOUT_IN_SEC ;

    while(digitalRead(ECHO) == LOW ) {      // Wait till the Burst will be send
      if( time (NULL) >= endwait) break;
    } Starttime();

    endwait = time (NULL) + uiMEASURE_TIMEOUT_IN_SEC ;
    while( digitalRead(ECHO) == HIGH ) {
      if( time (NULL) >= endwait) break;    // Wait for the (echo reponse) reflection from the object.
    } Endtime();

    // Ultraschall= 331,3 m/s | 33130 cm/s | 331300 mm/s | 331300000000 nm/s ;bei 0 Grad Celsius
    // Auschwingung des Sende und Empfangimpulses =>  0,000001170 cm/ns + Ultraschall geschwindigkeit => 0,000033130 cm/ns
    // Auf die gesamtstrecke addiert ergibt sich die Tatsächliche Geschwindigkeit von => 0,00003430 cm/ns
    // Hier errechnen wir die jedoch ein streckenabschnitt, das Empfangen => (0,00003430 cm/ns) / 2 -->  0,00001715 cm/ns
    const int iSonicSpeed= (34224 + 1170) / 2;  //Default: 0-Celsius:33130   18-Celsius:34224
    const float fElapsedNanoSec = ( ts_end.tv_nsec - ts_start.tv_nsec );         // Get the time difference: Send time - received time!
    return ( roundf( ( fElapsedNanoSec * iSonicSpeed * 100) / 100 ) - ( 0.5 * 1000000000) );
  }

  /*
    Function:
    Description:
  */
  float VolumeCylinder(float fCylinderHigh) {
    // formula: Volume=pi*radius*radius+high
    float fVolume= 0;
    static float fPi= 3.141592653589793;
    if( fCylinderHigh > 0 && fCisternaRadius > 0) {
      fVolume= fPi*fCisternaRadius*fCisternaRadius*fCylinderHigh;
    }
    return fVolume;
  }

  /*
    Function:
    Description:
  */
  float CurrentLevelInLiter(float fCurrentDistance, float *fMaxCisternaVolume, float *fMaxVolume, float *fCurrentVolume, float *fMinVolume) {
    float fCurrentLiter=0;
    *fMaxCisternaVolume    = VolumeCylinder(fCisternaHighMax);
    *fMaxVolume            = VolumeCylinder(fCisternaHighMax-fSurfaceOffSet);
    *fCurrentVolume        = VolumeCylinder(fCurrentDistance);
    *fMinVolume            = VolumeCylinder(fSuctionHead);

    if( *fMaxVolume > 0 && *fCurrentVolume > 0 ) fCurrentLiter= (*fMaxVolume - *fCurrentVolume )/1000;
    return fCurrentLiter;
  }

  /*
    Function:
    Description:
  */
  void *CalcAndSave(void *arg) {
    float fDistanceNM, fCalcDistAvarange[uiMEASURE_VAL];
    while(true) {
      for ( uint iSaveValue=0; iSaveValue <= uiMEASURE_VAL; iSaveValue++ ) {
        fDistanceNM =CalcDistance() + (fSurfaceOffSet * 1000000000 /*nanometer*/ ) + (fInternalOffSet * 1000000000);
        if (fDistanceNM >= 0 || bCalibration ) {
          if( ( (fDistanceNM /   1000000000 /*centimeter*/) >= 0 &&
                (fDistanceNM / 100000000000 /*meter*/) < 10 ) || (bCalibration) ) {
            if (iSaveValue < uiMEASURE_VAL) {
              if(bDebug) printf ("Measured value:  %i: %.2f cm \n", iSaveValue, fDistanceNM / 1000000000 );
              fCalcDistAvarange[iSaveValue]= fDistanceNM;
              Info_Msg_t.fDistanceLive = fDistanceNM / 1000000000;
            } else {

              float fCisCurrentVolumeInPercent, fMaxCisternaVolume, fMaxVolume, fCurrentVolume, fMinVolume;
              float fDistAvarange= CalcMedian(fCalcDistAvarange) / 1000000000;
              float fCisCurrentLiter= CurrentLevelInLiter( fDistAvarange,  &fMaxCisternaVolume, &fMaxVolume, &fCurrentVolume, &fMinVolume);
              fCisCurrentVolumeInPercent= fCisCurrentLiter / (fMaxVolume/1000 / 100);

              if(bDebug) {
                printf ("Time: %s\n",TimeStr().c_str());
                printf ("Distacne to the object: %.2f cm (Mediancalculation with %i measurements)\n", fDistAvarange, uiMEASURE_VAL);
                printf ("Current water level: %.2f%% (%.2f Liter of %2.f Liter)\n", fCisCurrentVolumeInPercent, fCisCurrentLiter, fMaxVolume/1000 );
              }

              Info_Msg_t.bValidInfo                = true;
              Info_Msg_t.bWaterSuctionRelease      = fCurrentVolume <= fMinVolume;
              Info_Msg_t.strDate                   = TimeStr();
              Info_Msg_t.fDistance                 = fDistAvarange > 0 ? fDistAvarange : 0 ;
              Info_Msg_t.iMeasurements             = uiMEASURE_VAL;

              Info_Msg_t.Distance_History.fC       = Info_Msg_t.Distance_History.fB > 0 ? Info_Msg_t.Distance_History.fB : 0;
              Info_Msg_t.Distance_History.fB       = Info_Msg_t.Distance_History.fA > 0 ? Info_Msg_t.Distance_History.fA : 0;
              Info_Msg_t.Distance_History.fA       = Info_Msg_t.fDistance           > 0 ? Info_Msg_t.fDistance           : 0;

              Info_Msg_t.CistVolInfo.fInPercent    = fCisCurrentVolumeInPercent > 0 ? fCisCurrentVolumeInPercent : 0;
              Info_Msg_t.CistVolInfo.fCurrentLiter = fCisCurrentLiter > 0 ? fCisCurrentLiter : 0;
              Info_Msg_t.CistVolInfo.fMinLiter     = 0;
              Info_Msg_t.CistVolInfo.fMaxLiter     = (fMaxVolume/1000) > 0 ? (fMaxVolume/1000) : 0;

            }
          } else if(bDebug) printf ("Out of Range! (Min: 0cm - Max: 1000 cm\n");
        } else if( bDebug ) printf ("CalcDistance - Error! The value is: %2.f\n",fDistanceNM / 1000000000);
        delay(uiDELAY_TIME_NEXT_MEASURE);
      }
    }
  }

  /*
    Function:
    Description:
  */
  struct PrintException {
    void operator()(exception_ptr exc) const {
      try {
             rethrow_exception(exc);
      } catch (const exception& e) {
        cerr << "An exception occured: " << e.what() << endl;
      }
    }
  };

  namespace Generic {
    void handleReady(const Rest::Request&, Http::ResponseWriter response) {
      response.send(Http::Code::Ok, "1");
    }
  };

  class CisternaService {

    public:
      CisternaService(Address addr)
          : httpEndpoint(std::make_shared<Http::Endpoint>(addr))
          , desc("Cisterna aPI", strVersion.c_str())
      { }

      void init(size_t thr = 2) {
          auto opts = Http::Endpoint::options()
              .threads(thr)
              .flags(Tcp::Options::InstallSignalHandler | Tcp::Options::ReuseAddr);
          httpEndpoint->init(opts);
          createDescription();
      }

      void start() {
          router.initFromDescription(desc);
          Rest::Swagger swagger(desc);
          swagger
              .uiPath("/api")
              .uiDirectory(strBinPath+"/dist")
              .apiPath("/rest.json")
              .serializer(&Rest::Serializer::rapidJson)
              .install(router);

          httpEndpoint->setHandler(router.handler());
          httpEndpoint->serve();
      }

      void shutdown() {
          httpEndpoint->shutdown();
      }

    private:
      void createDescription() {
        desc
            .info()
            .license("Apache", "http://www.apache.org/licenses/LICENSE-2.0");

        auto backendErrorResponse =
            desc.response(Http::Code::Internal_Server_Error, "An error occured with the backend");

        desc
            .schemes(Rest::Scheme::Http)
            .basePath("/v1")
            .produces(MIME(Application, Json))
            .consumes(MIME(Application, Json));

        desc
            .route(desc.get("/ready"))
            .bind(&Generic::handleReady)
            .response(Http::Code::Ok, "Response to the /ready call")
            .hide();

        auto versionPath = desc.path("/v1");
        auto valuesPath = versionPath.path("/values");

        valuesPath
            .route(desc.get("/SurfaceOffSet"))
            .bind(&CisternaService::SurfaceOffSetGet, this)
            .produces(MIME(Application, Json), MIME(Application, Xml))
            .response(Http::Code::Ok, "fSurfaceOffSet get..description ok.")
            .response(backendErrorResponse);
        /*valuesPath
            .route(desc.post("/SurfaceOffSet"), "set SurfaceOffSet")
            .bind(&CisternaService::SurfaceOffSetPost, this)
            .produces(MIME(Application, Json), MIME(Application, Xml))
            .consumes(MIME(Application, Json), MIME(Application, Xml))
            .parameter<Rest::Type::Float>("name", "The name of the account to create")
            .response(Http::Code::Ok, "fSurfaceOffSet post ..description ok.")
            .response(backendErrorResponse); */
        valuesPath
            .route(desc.get("/all"))
            .bind(&CisternaService::retrieveAllvalues, this)
            .produces(MIME(Application, Json), MIME(Application, Xml))
            .response(Http::Code::Ok, "The list of all measured values. The rormat is JSON.")
            .response(backendErrorResponse);
        valuesPath
            .route(desc.get("/state"))
            .bind(&CisternaService::retrieveState, this)
            .produces(MIME(Application, Json), MIME(Application, Xml))
            .response(Http::Code::Ok, "Current State. If a measured process was successful, will be true else this value is false.")
            .response(backendErrorResponse);
        valuesPath
            .route(desc.get("/watersuctionstate"))
            .bind(&CisternaService::retrieveWaterSuctionState, this)
            .produces(MIME(Application, Json), MIME(Application, Xml))
            .response(Http::Code::Ok, "Water suction state. The water level is OK, for Waterpump suction.")
            .response(backendErrorResponse);
        valuesPath
            .route(desc.get("/distance"))
            .bind(&CisternaService::retrieveDistance, this)
            .produces(MIME(Application, Json), MIME(Application, Xml))
            .response(Http::Code::Ok, "Last measured distance to the object. ")
            .response(backendErrorResponse);
        valuesPath
            .route(desc.get("/distancelive"))
            .bind(&CisternaService::retrieveDistanceLive, this)
            .produces(MIME(Application, Json), MIME(Application, Xml))
            .response(Http::Code::Ok, "Live measured distance to the object.")
            .response(backendErrorResponse);
        valuesPath
            .route(desc.get("/measurements"))
            .bind(&CisternaService::retrieveMeasurements, this)
            .produces(MIME(Application, Json), MIME(Application, Xml))
            .response(Http::Code::Ok, "Number of measurements to get the distance to the object.")
            .response(backendErrorResponse);
        valuesPath
            .route(desc.get("/distancehistory"))
            .bind(&CisternaService::retrieveDistanceHistory, this)
            .produces(MIME(Application, Json), MIME(Application, Xml))
            .response(Http::Code::Ok, "The last distance to object history. Where distance 'C' is the latest distance information. ")
            .response(backendErrorResponse);
        valuesPath
            .route(desc.get("/cistvolinpercent"))
            .bind(&CisternaService::retrieveCistVolInPercent, this)
            .produces(MIME(Application, Json), MIME(Application, Xml))
            .response(Http::Code::Ok, "The latest caculated cisterna volume. This vlaue is in percent.")
            .response(backendErrorResponse);
        valuesPath
            .route(desc.get("/cistvolinliter"))
            .bind(&CisternaService::retrieveCistVolInLiter, this)
            .produces(MIME(Application, Json), MIME(Application, Xml))
            .response(Http::Code::Ok, "The latest caculated cisterna volume. This vlaue is in Liter.")
            .response(backendErrorResponse);
        valuesPath
            .route(desc.get("/cistvolmininliter"))
            .bind(&CisternaService::retrieveCistVolMinInLiter, this)
            .produces(MIME(Application, Json), MIME(Application, Xml))
            .response(Http::Code::Ok, "Absolute minimum level in liter for the cisterna.")
            .response(backendErrorResponse);
        valuesPath
            .route(desc.get("/cistvolmaxinliter"))
            .bind(&CisternaService::retrieveCistVolMaxInLiter, this)
            .produces(MIME(Application, Json), MIME(Application, Xml))
            .response(Http::Code::Ok, "Absolute maximum level in liter for the cisterna.")
            .response(backendErrorResponse);
      }

      void SurfaceOffSetGet(const Rest::Request& req, Http::ResponseWriter response) {
          response.send(Http::Code::Ok, to_string(fSurfaceOffSet));
      }
      void SurfaceOffSetPost(const Rest::Request& req, Http::ResponseWriter response) {
          cout << "SurfaceOffSetPost" << endl;
          auto value = 99;
          //auto query = req.query();
          //string params = query.getParameter("name");
          //cout << "Using name parameter " << params << endl;

          //string body = req.body();
          //cout << "test message: body is " << body << endl;

          if(req.hasParam(":name")) {
            cout << "SurfaceOffSetPost Enter Val check." << endl;
            value = req.param(":name").as<float>();
            cout << "SurfaceOffSetPost Val: " << value << endl;
          }
          response.send(Http::Code::Ok, to_string(value));
      }

      void retrieveAllvalues(const Rest::Request& req, Http::ResponseWriter response) {
        char _id[20];
        StringBuffer JSONStrBuffer;
        Writer<StringBuffer> writer(JSONStrBuffer);
        writer.StartArray();

        for (int i = 0; i < 1; ++i) {
          writer.SetMaxDecimalPlaces(3);
          writer.StartObject();
          snprintf(_id, sizeof(_id), "item-%d", i);

          if(Info_Msg_t.bValidInfo) {

            writer.Key("State");                writer.Bool(Info_Msg_t.bValidInfo);
            writer.Key("WaterSuction");         writer.Bool(Info_Msg_t.bWaterSuctionRelease);

            writer.Key("Time");                 writer.String( Info_Msg_t.strDate.c_str());
            writer.Key("Distance");             writer.Double( Info_Msg_t.fDistance);
            writer.Key("Measurements");         writer.Uint(   Info_Msg_t.iMeasurements);

            writer.Key("Distance_History_A");   writer.Double( Info_Msg_t.Distance_History.fA);
            writer.Key("Distance_History_B");   writer.Double( Info_Msg_t.Distance_History.fB);
            writer.Key("Distance_History_C");   writer.Double( Info_Msg_t.Distance_History.fC);

            writer.Key("CistVol_InPercent");    writer.Double( Info_Msg_t.CistVolInfo.fInPercent    );
            writer.Key("CistVol_CurrentLiter"); writer.Double( Info_Msg_t.CistVolInfo.fCurrentLiter );
            writer.Key("CistVol_MinLiter");     writer.Double( Info_Msg_t.CistVolInfo.fMinLiter     );
            writer.Key("CistVol_MaxLiter");     writer.Double( Info_Msg_t.CistVolInfo.fMaxLiter     );

          } else {
            writer.Key("State");        writer.Bool(Info_Msg_t.bValidInfo);
            writer.Key("Info");         writer.String("Please wait. The initial Measurement takes a while.");
          } 
          writer.EndObject();
        }
        writer.EndArray();
        response.send(Http::Code::Ok, JSONStrBuffer.GetString());
      }

      void retrieveState(const Rest::Request& req, Http::ResponseWriter response) {
          response.send(Http::Code::Ok, Info_Msg_t.bValidInfo ? "true" : "false");
      }

      void retrieveWaterSuctionState(const Rest::Request& req, Http::ResponseWriter response) {
          response.send(Http::Code::Ok, Info_Msg_t.bWaterSuctionRelease ? "true" : "false");
      }

      void retrieveDistance(const Rest::Request& req, Http::ResponseWriter response) {
          response.send(Http::Code::Ok, to_string(Info_Msg_t.fDistance) );
      }

      void retrieveDistanceLive(const Rest::Request& req, Http::ResponseWriter response) {
          response.send(Http::Code::Ok, to_string(Info_Msg_t.fDistanceLive) );
      }

      void retrieveMeasurements(const Rest::Request& req, Http::ResponseWriter response) {
          response.send(Http::Code::Ok, to_string(Info_Msg_t.iMeasurements) );
      }
      void retrieveDistanceHistory(const Rest::Request& req, Http::ResponseWriter response) {
          response.send(Http::Code::Ok,
            "A: "+to_string(Info_Msg_t.Distance_History.fA)+"\n"+
            "B: "+to_string(Info_Msg_t.Distance_History.fB)+"\n"+
            "C: "+to_string(Info_Msg_t.Distance_History.fC)
            );
      }
      void retrieveCistVolInPercent(const Rest::Request& req, Http::ResponseWriter response) {
          response.send(Http::Code::Ok, to_string(Info_Msg_t.CistVolInfo.fInPercent) );
      }
      void retrieveCistVolInLiter(const Rest::Request& req, Http::ResponseWriter response) {
          response.send(Http::Code::Ok, to_string(Info_Msg_t.CistVolInfo.fCurrentLiter) );
      }
      void retrieveCistVolMinInLiter(const Rest::Request& req, Http::ResponseWriter response) {
          response.send(Http::Code::Ok, to_string(Info_Msg_t.CistVolInfo.fMinLiter) );
      }
      void retrieveCistVolMaxInLiter(const Rest::Request& req, Http::ResponseWriter response) {
          response.send(Http::Code::Ok, to_string(Info_Msg_t.CistVolInfo.fMaxLiter) );
      }

      void onTimeout(const Http::Request& req, Http::ResponseWriter response) {
          response.send(Http::Code::Request_Timeout, "Timeout")
                  .then([=](ssize_t) { }, PrintException());
      }

      std::shared_ptr<Http::Endpoint> httpEndpoint;
      Rest::Description desc;
      Rest::Router router;
  };


/*
  Function: main
  Description:
*/
int main (int argc, char *argv[] ) {
  cout << "--------------------------------------------" << endl;
  cout <<  strVersion.c_str() << endl;
  cout << "--------------------------------------------" << endl;

  int thr = 2;                                      // Number of threads for the API-Server
  Port port(9080);                                  // Default http port for the API-Server

  uint uiMeasureValues    = 100;                    // Count of measurements for calculating the median value
  uint uiMeasureTimeout   =   1;                    // Timeout in second. Time to wait for the response. Default is 1 Second (33130cm/s). The object is not present or seems to be to far.

  uint uiDelayWriteLow          = 100;
  uint uiDelayWriteHigh         =  10;
  uint uiDelayWriteLowAfterHigh =  10;

  int iOptions            =  -1;
  const char * short_opt  = "hp:t:dcg:e:y:v:o:l:m:n:s:q:r:u:w:i:";
  struct option long_opt[]= {
     { "help"          , no_argument      , NULL, 'h' },
     { "WLDelay"       , required_argument, NULL, 'u' },
     { "WHelay"        , required_argument, NULL, 'w' },
     { "WLAHDelay"     , required_argument, NULL, 'i' },
     { "port"          , required_argument, NULL, 'p' },
     { "threads"       , required_argument, NULL, 't' },
     { "debug"         , optional_argument, NULL, 'd' },
     { "calibration"   , optional_argument, NULL, 'c' },
     { "trigger"       , required_argument, NULL, 'g' },
     { "echo"          , required_argument, NULL, 'e' },
     { "delay"         , required_argument, NULL, 'y' },
     { "values"        , required_argument, NULL, 'v' },
     { "timeout"       , required_argument, NULL, 'o' },
     { "SurfaceOffSet" , required_argument, NULL, 'l' },
     { "InternalOffSet", required_argument, NULL, 'm' },
     { "CistHighMax"   , required_argument, NULL, 'n' },
     { "CistRadius"    , required_argument, NULL, 's' },
     { "SuctionHead"   , required_argument, NULL, 'r' },
     { NULL         , 0                , NULL, 0   }
  };

  while((iOptions = getopt_long(argc, argv, short_opt, long_opt, NULL)) != -1) {
    switch(iOptions) {
      case 'u': /* WriteLow */           uiMEASURE_DELAYWRITELOW = stoi(optarg);      break;
      case 'w': /* WriteHigh */         uiMEASURE_DELAYWRITEHIGH = stoi(optarg);      break;
      case 'i': /* WriteLowAfterHigh */ uiMEASURE_DELAYWRITELOWHIGH = stoi(optarg);   break;
      case 'p': /* port */                                  port = std::atoi(optarg); break;
      case 't': /* thread */                                 thr = stol(optarg);      break;
      case 'd': /* debug */                               bDebug = true;              break;
      case 'c': /* Calibration */                   bCalibration = true;              break;
      case 'g': /* TRIGGER */                            TRIGGER = stol(optarg);      break;
      case 'e': /* echo */                                  ECHO = stol(optarg);      break;
      case 'l': /* fSurfaceOffSet */              fSurfaceOffSet = stof(optarg);      break;
      case 'm': /* fInternalOffSet */            fInternalOffSet = stof(optarg);      break;
      case 'n': /* fCisternaHighMax */          fCisternaHighMax = stof(optarg);      break;
      case 's': /* fCisternaRadius */            fCisternaRadius = stof(optarg);      break;
      case 'r': /* fSuctionHead */                  fSuctionHead = stof(optarg);      break;
      case 'y': /* delay time */       uiDELAY_TIME_NEXT_MEASURE = stoi(optarg);      break; // set Measure DelayTime. Default is 50ms
      case 'v': /* values */                       uiMEASURE_VAL = stoi(optarg);      break; // set Measure Values. Default is 100
      case 'o': /* timeout */           uiMEASURE_TIMEOUT_IN_SEC = stoi(optarg);      break; // set Measure wait Timeout. Default is 1 Second.
      case 'h':
        cout << "Usage: "<< argv[0] << " [OPTIONS]" << endl;
        cout << "  -h --help             This Help!" << endl;
        cout << "  -p --port             Default http port for the API-Server (Default: 9080        )" << endl;
        cout << "  -t --threads          Number of threads for the API-Server (Default: 2           )" << endl;
        cout << "  -d --debug            Enable Debug Output                  (Default: false       )" << endl;
        cout << "  -c --calibration      Enable Calibration Mode              (Default: false       )" << endl;
        cout << "  -g --trigger          TRIGGER PIN WiringPI                 (Default: 4 (GPIO:23) )" << endl;
        cout << "  -e --echo             ECHO PIN WiringPI                    (Default: 5 (GPIO:24) )" << endl;
        cout << "  -y --delay            Measure DelayTime                    (Default: 50ms        )" << endl;
        cout << "  -v --values           Measure Values for MedianCalc        (Default: 100         )" << endl;
        cout << "  -o --timeout          Measure wait timeout                 (Default: 1 Second    )" << endl;
        cout << "  -l --SurfaceOffSet    Sensor offset (+ or -) to sureface   (Default: 0.00 cm     )" << endl;
        cout << "  -m --InternalOffSet   Internal offset supersonic device    (Default: -2.00 cm    )" << endl;
        cout << "  -n --CistHighMax      Maximum cisterna high                (Default: 223.00 cm   )" << endl;
        cout << "  -s --CistRadius       radius of the Cisterna               (Default: 103.50 cm   )" << endl;
        cout << "  -r --SuctionHead      ground sediment suction height       (Default: 20.00 cm    )" << endl;
        cout << "  -u --WLDelay          Delay Write Low   us:MicroSecond     (Default: 500 us      )" << endl;
        cout << "  -a --WHelay           Delay Write High                     (Default: 10 us       )" << endl;
        cout << "  -i --WLAHDelay        Delay Write Low after High           (Default: 10 us       )" << endl;
        cout << endl;
        cout << "Usage Example 1: "<< argv[0] << " -p 9080 -t 2 -dc -g 4 -e 5 -y 50 -v 100 -o 1 -l 16.00 -m 2.00 -n 223.00 -s 103.50 -q 16.00 -r 20.00" << endl;
        cout << "Usage Example 2: "<< argv[0] << " -p 9080 -t 2 -dc -g 4 -e 5 -y 50 -v 100 -o 1 " << endl;
        cout << "Usage Example 3: "<< argv[0] << " -dc" << endl;
        cout << endl;
      return 0;

      case -1:       /* no more arguments */
      case  0:       /* long options toggles */
      break;
      default:
        cout << stderr << " " << argv[0] << ": invalid option -- " << iOptions << endl;
        cout << stderr << " Try '" << argv[0] << " --help' for more information." << endl;
      return(-2);
    }
  }

  if( InitWiringPi() && getBinPath() ) {
    cout << "WiringPi setup and getBinPath was successful." << endl;
    pthread_t pThread;
    if(pthread_create(&pThread, 0, CalcAndSave, 0) != 0) {
      cout <<  "Error: pthread_create() failed" << endl;
      exit(EXIT_FAILURE);
    } else cout << "Ultrasonic measurmend thread started." << endl;

    Info_Msg_t.bValidInfo=false;

    cout << "--------------------------------------------" << endl;
    cout << "Cores               : " << hardware_concurrency() << endl;
    cout << "Threads Using       : " << thr << " threads" << endl;
    cout << "Http Port           : " << port << endl;
    cout << "Debug Output        : " << (bDebug ? "enable":"disabled") << endl;
    cout << "Calibration Mode    : " << (bCalibration ? "enable":"disabled") << endl;
    cout << "WiringPI - TRIGGER  : " << TRIGGER << endl;
    cout << "WiringPI - ECHO     : " << ECHO << endl;
    cout << "Measurements Delay  : " << uiDELAY_TIME_NEXT_MEASURE << endl;
    cout << "Measurements Count  : " << uiMEASURE_VAL << endl;
    cout << "Measurements Timeout: " << uiMEASURE_TIMEOUT_IN_SEC << endl;
    cout << "Sleep Write Low     : " << uiMEASURE_DELAYWRITELOW << endl;
    cout << "Sleep Write High    : " << uiMEASURE_DELAYWRITEHIGH << endl;
    cout << "Sleep Write Low High: " << uiMEASURE_DELAYWRITELOWHIGH << endl;
    cout << "--------------------------------------------" << endl;

    Address addr(Ipv4::any(), port);
    cout << "Starting Cisterna API-Server..." << endl;
    CisternaService server(addr);

    cout << "Initializing the Cisterna API-Server..." << endl;
    server.init(thr);

    cout << "Cisterna API-Server is started." << endl;
    server.start();

    cout << "Shutdowning Cisterna API-Server" << endl;
    server.shutdown();
  }

  return 0;
}
