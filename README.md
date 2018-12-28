
![Alt Text](https://media.giphy.com/media/8lHof6a8CWHJF5Xo9m/giphy.gif)

# cisterna
***
[Cisterna] pi is a native c command line application for raspberry pi with a restful web api. It is designed to measure the distance to water or any fluid level in the tank using a [Ultrasonic Sensor] or also possible to use [Laser Snesor]. It comes with a simple user interface based on [Swagger UI]  with a minimal visualisation of the current measurement and volume Information.


### REST API
```sh
http://yourip/api
```
![Alt Text](https://media.giphy.com/media/fQuIxoQpyOnKGaLxCW/giphy.gif)

### Installation and configuration
```sh
git clone https://github.com/GeminiServer/Cisterna.git
cd Cisterna/release
```
Copy the content of the release folder to your device. Open the file /etc/cisterna.conf with a editor of your choice and check or set your settings:
```sh
$ nano /etc/cisterna.conf
```
<details><summary>cisterna.conf - Configuration Description and file example</summary>
<p>

```
<!-- language: lang-none -->
Info: Cistern is at 100%, if the water level of the ceiling has 16cm distance. The height of the cisterne is 223.00cm. Diameter of the cistern is: 207.00cm
//|----------------------|
//|    (_) SENSOR (_)    |
//|----------------------|   --> Max. high (fCisternaMaxHigh)  / Max. volume (fCisternaMaxVolume)
//||||||||||||||||||||||||   --> Overflow difference (fSurfaceOffSet) Offset overflow!
//|----------------------|   --> Surface / Overflow Height / Overflow Volume 
//|----------------------|   --> Intake START == 100%
//|()()()()()()()()()()()|   }
//|()()()()()()()()()()()|   }
//|()()()()()()()()()()()|   } Usable water: height and volume
//|()()()()()()()()()()()|   }
//|()()()()()()()()()()()|   }
//|----------------------|  --> Sediment height (fSuctionHead) = intake END!
//||||||||||||||||||||||||
//|----------------------|  --> ZERO Cisterna
```

| Setting | Description | Value|
| ------ | ----------- |---|
|update-interval    | Read and write timer in Minutes. Default is 10 minutes.       | minute     |
  -h --help             |This Help!
  -p --port             |Default http port for the API-Server |Default: 9080        
  -t --threads          |Number of threads for the API-Server |Default: 2           
  -d --debug            |Enable Debug Output                  |Default: false       
  -c --calibration      |Enable Calibration Mode              |Default: false       
  -g --trigger          |TRIGGER PIN WiringPI                 |Default: 4 (GPIO:23) 
  -e --echo             |ECHO PIN WiringPI                    |Default: 5 (GPIO:24) 
  -y --delay            |Measure DelayTime                    |Default: 50ms        
  -v --values           |Measure Values for MedianCalc        |efault: 100         
  -o --timeout          |Measure wait timeout                 |Default: 1 Second    
  -l --SurfaceOffSet    |Sensor offset (+ or -) to sureface   |Default: 0.00 cm     
  -m --InternalOffSet   |Internal offset supersonic device    |Default: -2.00 cm    
  -n --CistHighMax      |Maximum cisterna high                |Default: 223.00 cm   
  -s --CistRadius       |radius of the Cisterna               |Default: 103.50 cm   
  -r --SuctionHead      |ground sediment suction height       |Default: 20.00 cm    
  -u --WLDelay          |Delay Write Low   us:MicroSecond     |Default: 500 us      
  -a --WHelay           |Delay Write High                     |Default: 10 us       
  -i --WLAHDelay        |Delay Write Low after High           |Default: 10 us       
Here is a example of the cisterna.conf file: 
```sh
####
# Cisterna Settings
####
# Info: For Port 80 use iptables rule redirect to 9080
# i.e.: sudo iptables -A PREROUTING -t nat -p tcp --dport 80 -j REDIRECT --to-ports 9080

CISTERNA_ARG=\
 --port=9080\
 --threads=2\
 --delay=50\
 --values=50\
 --timeout=1\
 --SurfaceOffSet=0\
 --InternalOffSet=1.2
```
</p>
</details>

After your configurations is set, just start the cisterna:
```sh
sudo systemctl daemon-reload
sudo systemctrl start cisterna
```

[Swagger UI]: <https://swagger.io/tools/swagger-ui/>
[Laser Sensor]: <https://de.wikipedia.org/wiki/Elektrooptische_Entfernungsmessung>
[Ultrasonic Sensor]: <https://de.wikipedia.org/wiki/Ultraschall>
[cisterna]: <https://en.wikipedia.org/wiki/Cistern>
