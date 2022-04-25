# Fanpico: Command Reference
Fanpico uses "SCPI like" command set. Command syntax should be mostly SCPI and IEE488.2 compliant,
to make it easier to control and configure Fanpico units.


## Command Set
Fanpico supports following commands:

* [*IDN?](#idn)
* [*RST](#rst)
* [CONFigure?](#configure)
* [CONFigure:SAVe](#configuresave)
* [CONFigure:Read?](#configureread)
* [CONFigure:DELete](#configuredelete)
* [CONFigure:FANx:NAME](#configurefanxname)
* [CONFigure:FANx:NAME?](#configurefanxname-1)
* [CONFigure:FANx:MINpwm](#configurefanxminpwm)
* [CONFigure:FANx:MINpwm?](#configurefanxminpwm-1)
* [CONFigure:FANx:MAXpwm](#configurefanxmaxpwm)
* [CONFigure:FANx:MAXpwm?](#configurefanxmaxpwm-1)
* [CONFigure:FANx:PWMCoeff](#configurefanxpwmcoeff)
* [CONFigure:FANx:PWMCoeff?](#configurefanxpwmcoeff-1)
* [CONFigure:FANx:RPMFactor](#configurefanxrpmfactor)
* [CONFigure:FANx:RPMFactor?](#configurefanxrpmfactor-1)
* [CONFigure:FANx:SOUrce](#configurefanxsource)
* [CONFigure:FANx:SOUrce?](#configurefanxsource-1)
* [CONFigure:FANx:PWMMap](#configurefanxpwmmap)
* [CONFigure:FANx:PWMMap?](#configurefanxpwmmap-1)
* [CONFigure:MBFANx:NAME](#configurembfanxname)
* [CONFigure:MBFANx:NAME?](#configurembfanxname-1)
* [CONFigure:MBFANx:MINrpm](#configurembfanxminrpm)
* [CONFigure:MBFANx:MINrpm?](#configurembfanxminrpm-1)
* [CONFigure:MBFANx:MAXrpm](#configurembfanxmaxrpm)
* [CONFigure:MBFANx:MAXrpm?](#configurembfanxmaxrpm-1)
* [CONFigure:MBFANx:RPMCoeff](#configurembfanxrpmcoeff)
* [CONFigure:MBFANx:RPMCoeff?](#configurembfanxrpmcoeff-1)
* [CONFigure:MBFANx:RPMFactor](#configurembfanxrpmfactor)
* [CONFigure:MBFANx:RPMFactor?](#configurembfanxrpmfactor-1)
* [CONFigure:MBFANx:SOUrce](#configurembfanxsource)
* [CONFigure:MBFANx:SOUrce?](#configurembfanxsource-1)
* [CONFigure:MBFANx:RPMMap](#configurembfanxrpmmap)
* [CONFigure:MBFANx:RPMMap?](#configurembfanxrpmmap-1)
* [CONFigure:SENSORx:NAME](#configuresensorxname)
* [CONFigure:SENSORx:NAME?](#configuresensorxname-1)
* [CONFigure:SENSORx:TEMPOffset](#configuresensorxtempoffset)
* [CONFigure:SENSORx:TEMPOffset?](#configuresensorxtempoffset-1)
* [CONFigure:SENSORx:TEMPCoeff](#configuresensorxtempcoeff)
* [CONFigure:SENSORx:TEMPCoeff?](#configuresensorxtempcoeff-1)
* [CONFigure:SENSORx:TEMPMap](#configuresensorxtempmap)
* [CONFigure:SENSORx:TEMPMap?](#configuresensorxtempmap-1)
* [CONFigure:SENSORx:BETAcoeff](#configuresensorxbetacoeff)
* [CONFigure:SENSORx:BETAcoeff?](#configuresensorxbetacoeff-1)
* [CONFigure:SENSORx:THERmistor](#configuresensorxthermistor)
* [CONFigure:SENSORx:THERmistor?](#configuresensorxthermistor-1)
* [CONFigure:SENSORx:TEMPNominal](#configuresensorxtempnominal)
* [CONFigure:SENSORx:TEMPNominal?](#configuresensorxtempnominal-1)
* [MEASure:Read?](#measureread)
* [MEASure:FANx?](#measurefanx)
* [MEASure:FANx:Read?](#measurefanxread)
* [MEASure:FANx:RPM?](#measurefanxrpm)
* [MEASure:FANx:PWM?](#measurefanxpwm)
* [MEASure:FANx:TACho?](#measurefanxtacho)
* [MEASure:MBFANx?](#measurembfanx)
* [MEASure:MBFANx:Read?](#measurembfanxread)
* [MEASure:MBFANx:RPM?](#measurembfanxrpm)
* [MEASure:MBFANx:PWM?](#measurembfanxpwm)
* [MEASure:MBFANx:TACho?](#measurembfanxtacho)
* [MEASure:SENSORx?](#measuresensorx)
* [MEASure:SENSORx:Read?](#measuresensorxread)
* [MEASure:SENSORx:TEMP?](#measuresensorxtemp)
* [Read?](#read)
* [SYStem:DEBug](#systemdebug)
* [SYStem:DEBug?](#systemdebug)
* [SYStem:ECHO](#systemecho)
* [SYStem:ECHO?](#systemecho)
* [SYStem:FANS?](#systemfans)
* [SYStem:LED](#systemled)
* [SYStem:LED?](#systemled-1)
* [SYStem:MBFANS?](#systemmbfans)
* [SYStem:SENSORS?](#systemsensors)
* [SYStem:UPGRADE](#systemupgrade)
* [SYStem:VERsion?](#systemversion)

Additionally unit will respond to following standard SCPI commands to provide compatiblity in case some program
unconditionally will send these:
* *CLS
* *ESE
* *ESE?
* *ESR?
* *OPC
* *OPC?
* *SRE
* *SRE?
* *STB?
* *TST?
* *WAI


### Common Commands

#### *IDN?
Identify device. This returns string that contains following fields:
```
<manufacturer>,<model number>,<serial number>,<firmware version>
```

Example:
```
*IDN?
TJKO Industries,FANPICO-0804,e660c0d1c768a330,1.0
```

#### *RST
  Reset unit. This triggers Fanpico to perform (warm) reboot.
  
  ```
  *RST
  ```
  
### CONFigure:
Commands for configuring the device settings.

#### CONFigure?
  Display current configuration in JSON format.
  Same as CONFigure:Read?
  
  Example:
  ```
  CONF?
  ```
  
#### CONFigure:SAVe
Save current configuration into flash memory.

Example:
```
CONF:SAVE
```

#### CONFigure:Read?
Display current configuration in JSON format.

Example:
```
CONF:READ?
```

#### CONFigure:DELete
Delete current configuration saved into flash. After unit has been reset it will be using default configuration.

Example:
```
CONF:DEL
*RST
```

### CONFigure:FANx Commands
FANx commands are used to configure specific fan output port.
Where x is a number from 1 to 8.

For example:
```
CONFIGURE:FAN8:NAME Exhaust Fan 2
```

#### CONFigure:FANx:NAME
Set name for fan (output) port.

For example:
```
CONF:FAN1:NAME CPU Fan 1
```

#### CONFigure:FANx:NAME?
Query name of a fan (output) port.
  
For example:
```
CONF:FAN1:NAME?
CPU Fan 1
```

#### CONFigure:FANx:MINpwm
Set absolute minimum PWM duty cycle (%) for given fan port.
This can be used to make sure that fan always sees a minimum
duty cycle (overriding the normal fan signal).

Default: 0 %
 
Example: Set minimum PWM duty cycle to 20% for FAN1
```
CONF:FAN1:MIN 20
```

#### CONFigure:FANx:MINpwm?
Query current minimum PWM duty cycle (%) configured on a fan port.
 
Example: 
```
CONF:FAN1:MIN?
20
```

#### CONFigure:FANx:MAXpwm
Set absolute maximum PWM duty cycle (%) for given fan port.
This can be used to make sure that fan never sees higher duty cycle
than given value (overriding the normal fan signal).

Default: 100 %
 
Example: Set maximum PWM duty cycle to 95% for FAN1
```
CONF:FAN1:MAX 95
```

#### CONFigure:FANx:MAXpwm?
Query current maximum PWM duty cycle (%) configured on a fan port.
 
Example: 
```
CONF:FAN1:MAX?
95
```

#### CONFigure:FANx:PWMCoeff
Set scaling factor for the fan PWM (output) signal.
This is applied to the PWM duty cycle before MAXpwm and MINpwm limits are applied.

Default: 1.0
 
Example: Set FAN6 to run 20% slower than signal its configured to follow.
```
CONF:FAN6:PWMC 0.8
```

#### CONFigure:FANx:PWMCoeff?
Query current PWM duty cycle (%) scaling factor configured on a fan port.
 
Example: 
```
CONF:FAN6:PWMC?
0.8
```

#### CONFigure:FANx:RPMFactor
Set number of pulses fan generates per one revolution.
This is used to calculate RPM measurement based on the Tachometer
signal from the fan.

PC Fans typically send 2 pulses per revolution (per Intel specifications).

Default: 2
 
Example: Adjust factor for a fan that produces 4 pulses per revolution
```
CONF:FAN1:RPMF 4
```

#### CONFigure:FANx:RPMFactor?
Query current RPM conversion factor configured on a fan port.
 
Example: 
```
CONF:FAN1:RPMF?
4
```

#### CONFigure:FANx:SOUrce
Configure source for the PWM signal of a fan.

Source types:
* MBFAN (set fan to follow duty cycle from motherboard fan port)
* SENSOR (set fan to follow temperature based duty cycle)
* FAN (set fan to follow another FAN output duty cycle)
* FIXED (set fan to run on fixed duty cycle)


Defaults:
FAN|SOURCE
---|------
1|MBFAN,1
2|MBFAN,2
3|MBFAN,3
4|MBFAN,4
5|MBFAN,1
6|MBFAN,2
7|MBFAN,3
8|MBFAN,4
 
Example: Set FAN 5 to follow temperature sensor 2
```
CONF:FAN5:SOURCE SENSOR,2
```

Example: Set FAN 6 to run always at fixed 50% duty cycle (speed)
```
CONF:FAN6:SOURCE FIXED,50
```

#### CONFigure:FANx:SOUrce?
Query current signal source for a fan.

Command returns response in following format:
```
source_type,source_no
```
 
Example: 
```
CONF:FAN1:SOU?
mbfan,1
```

#### CONFigure:FANx:PWMMap
Set mapping (curve) for the PWM duty cycle signal.
This can be used to customize how fan will respond to input signal it receives.

Mapping is specified with up to 32 points (that can be plotted as a curve)
that map the relation of the input signal (x value) to output signal (y value).
Mapping should at minimum include that start and end points of the expected input signal
(typically 0 and 100).

Default mapping is linear (1:1) mapping:
x|y
-|-
0|0
100|100

Example: Configure Fan to run at 20% duty cycle between 0-20% input signal then scale
linearly to 100% by the time input signal reaches 100%
```
CONF:FAN1:PWMMAP 0,20,20,20,100,100
```

Example: Assuming we have "fancy" PWM fan that actually stops spinning on 0% duty cycle.
And we want fan not run until input PWM signal duty cycle is over 30%, and we want fan to reach
full speed by the time 90% duty signal is received.
```
CONF:FAN1:PWMMAP 0,0,30,0,90,100
```

#### CONFigure:FANx:PWMMap?
Display currently active mapping (curve) for the PWM duty signal.

Mapping is displayed as comma separated list of values:
```
x_1,y_1,x_2,y_2,...,x_n,y_n
```

For example:
```
CONF:FAN1:PWMMAP?
0,0,100,100
```


### CONFigure:MBFANx Commands
MBFANx commands are used to configure specific motherboard fan input port.
Where x is a number from 1 to 4.

For example:
```
CONF:MBFAN4:NAME Water Pump
```

#### CONFigure:MBFANx:NAME
Set name for motherboard fan (input) port.

For example:
```
CONF:MBFAN1:NAME CPU Fan
```

#### CONFigure:MBFANx:NAME?
Query name of a motherboard fan (input) port.
  
For example:
```
CONF:MBFAN1:NAME?
CPU Fan
```

#### CONFigure:MBFANx:MINrpm
Set absolute minimum RPM value for given motherboard fan port.
This can be used to make sure motherboard never sees "fan"
running slower than this value (even if fan is not running at all).

Default: 0
 
Example: Set minimum RPM (sent to motheboard) to be at least 500
```
CONF:MBFAN1:MIN 500
```

#### CONFigure:MBFANx:MINrpm?
Query current minimum RPM value configured on a motherboard fan port.
 
Example: 
```
CONF:MBFAN1:MIN?
500
```

#### CONFigure:MBFANx:MAXrpm
Set absolute maximum value for given motherboard fan port.
This can be used to make sure motherboard never sees "fan"
running ffaster than this value.

Default: 10000
 
Example: Set maximum RPM value motheboard can see to 3000
```
CONF:MBFAN1:MAX 3000
```

#### CONFigure:MBFANx:MAXrpm?
Query current maximum RPM value configured on a motherboard fan port.
 
Example: 
```
CONF:MBFAN1:MAX?
3000
```

#### CONFigure:MBFANx:RPMCoeff
Set scaling factor for the motherboard fan Tachometer (output) signal.
This is applied to adjust the output RPM (and tachometer signal frequency).

Default: 1.0
 
Example: Scale the MBFAN4 port Tachometer (RPM) values to be 20% higher than input.
```
CONF:MBFAN4:RPMC 1.2
```

#### CONFigure:MBFANx:RPMCoeff?
Query current TAchometer (RPM) scaling factor configured on a motherboard fan port.
 
Example: 
```
CONF:MBFAN6:RPMC?
1.2
```

#### CONFigure:MBFANx:RPMFactor
Set number of pulses per one revolution in the generated tachometer signal (going out to motherboard).
This is mainly only needed if using Fanpico with something else than PC motherboard, that expects
to see tachometer signal from Fans that produce other than the default 2 pulses per revolution.

Default: 2
 
Example: Adjust factor for (emulating fan that produces 4 pulses per revolution
```
CONF:MBFAN1:RPMF 4
```

#### CONFigure:MBFANx:RPMFactor?
Query current RPM conversion factor configured on a motherboard fan port.
 
Example: 
```
CONF:MBFAN1:RPMF?
4
```

#### CONFigure:MBFANx:SOUrce
Configure source for the Tachometer (RPM) signal for a motheboard fan (output) port.

Source types:
* FAN (signal received from a fan)
* FIXED (static signal at given RPM)


Defaults:
MBFAN|SOURCE
---|------
1|FAN,1
2|FAN,2
3|FAN,3
4|FAN,4
 
Example: Set MBFAN 2 to follow Tachometer signal from FAN8
```
CONF:MBFAN2:SOURCE FAN,8
```

Example: Set MBFAN 4 to see fixed 1500 RPM tachometer signal
```
CONF:MBFAN4:SOURCE FIXED,1500
```

#### CONFigure:MBFANx:SOUrce?
Query current signal source for a motherboar fan (Tachometer output).

Command returns response in following format:
```
source_type,source_no
```
 
Example: 
```
CONF:MBFAN1:SOU?
fan,1
```

#### CONFigure:MBFANx:RPMMap
Set mapping (curve) for the Tachometer (RPM) output signal to motherboard.
This can be used to customize what motherboard sees as the "fan" RPM.

Mapping is specified with up to 32 points (that can be plotted as a curve)
that map the relation of the input signal (x value) to output signal (y value).
Mapping should at minimum include that start and end points of the expected input signal
(typically 0 and 100).

Default mapping is linear (1:1) mapping:
x|y
-|-
0|0
10000|10000

Example: Assuming we have configured FAN1 to only run above 30% PWM signal, but our 
motherboard sets alarm when it doesnt detect fan running, we can provide 'fake' 500RPM
speed to the motherboard until fan is actually running at least 500 RPM...
```
CONF:MBFAN1:PWMMAP 0,500,500,500,10000,10000
```

#### CONFigure:MBFANx:RPMMap?
Display currently active mapping (curve) for the tachometer signal sent to motherboard
fan port.

Mapping is displayed as comma separated list of values:
```
x_1,y_1,x_2,y_2,...,x_n,y_n
```

For example:
```
CONF:MBFAN1:RPMMAP?
0,0,10000,10000
```

### CONFigure:SENSORx Commands
SENSORx commands are used to configure specific (temperature) sensor.
Where x is a number from 1 to 3.

Sensor|Description
------|-----------
1|Thermistor connected to SENSOR1 connector.
2|Thermistor connected to SENSOR2 connector.
3|Internal temperature sensor on RP2040 (can be used for "Ambient" temperature measurementes as RP2040 doesn't heat up significantly).

For example:
```
CONF:SENSOR1:NAME Air Intake
```

#### CONFigure:SENSORx:NAME
Set name for temperature sensor.

For example:
```
CONF:SENSOR1:NAME Air Intake
```

#### CONFigure:SENSORx:NAME?
Query name of a temperature sensor.
  
For example:
```
CONF:SENSOR1:NAME?
Air Intake
```

#### CONFigure:SENSORx:TEMPOffset
Set offset that is applied to measured temperature.
This can be used as a grude calibration method by setting offset to 
the difference of actual temperature vs. what sensor is reporting.

Default: 0.0

For example: Sensor is reporting 2.5 degrees "high", so set offset to -2.5:
```
CONF:SENSOR1:TEMPO -2.5
```

#### CONFigure:SENSORx:TEMPOffset?
Get the current temperature offset configured for the sensor.
  
For example:
```
CONF:SENSOR1:TEMPO?
-2.5
```

#### CONFigure:SENSORx:TEMPCoeff
Set coefficient that is used to multiply the (raw) temperature sensor reports.
This can be used to correct error in temperature measurements.

Default: 1.0

For example: Sensor is consistently reading 10% higher than actual temperature
```
CONF:SENSOR1:TEMPC 0.9
```

#### CONFigure:SENSORx:TEMPCoeff?
Get the current temperature coefficient configured for the sensor.
  
For example:
```
CONF:SENSOR1:TEMPC?
0.9
```

#### CONFigure:SENSORx:TEMPMap
Set mapping (curve) for converting temperature to PWM signal duty cycle (%).
This can be used to customize how temperature affects fan speed.

Mapping is specified with up to 32 points (that can be plotted as a curve)
that map the relation of the temperature (x value) to output PWM signal (y value).
Mapping should at minimum include that start and end points of the expected input signal.


Default mapping:
x (Temperature in C)|y (Fan Duty Cycle in %)
-|-
20|0
50|100

Example: Set Fan to start running at 20% duty cycle at 25C and to be full speed at 50C
```
CONF:SENSOR1:TEMPMAP 25,20,50,100
```

#### CONFigure:SENSORx:TEMPMap?
Display currently active mapping (curve) for convertin measured temperature
to PWM duty cycle (%) for the fan output signal.

Mapping is displayed as comma separated list of values:
```
x_1,y_1,x_2,y_2,...,x_n,y_n
```

For example:
```
CONF:SENSOR1:TEMPMAP?
20,0,50,10000
```

#### CONFigure:SENSORx:BETAcoeff
Set beta coefficient of thermistor used to measure temperatur.
Typically thermistor beta coefficient is in the range 3000-4000.

Default: 3950

For example:
```
CONF:SENSOR1:BETA 3950
```

#### CONFigure:SENSORx:BETAcoeff?
Get the configured beta coefficient of the thermistor used to measure temperature.

For example:
```
CONF:SENSOR1:BETA?
3950
```

#### CONFigure:SENSORx:THERmistor
Set thermistor nominal resistance at nominal (room) temperature.
Typically this is either 10000 (for 10k thermistors) or 100000 (for 100k thermistors).

Default: 10000

For example: configure 100k thermistor
```
CONF:SENSOR1:THER 100000
```

#### CONFigure:SENSORx:THERmistor?
Get the thermistor nominal resistance at nominal (room) temperature.

For example:
```
CONF:SENSOR1:THER?
100000
```

#### CONFigure:SENSORx:TEMPNominal
Set nominal temperature of the thermistor (in C).
Typically nominal temperature is around room temperature.

Default: 25.0

For example:
```
CONF:SENSOR1:TEMPN 25.0
```

#### CONFigure:SENSORx:TEMPNominal?
Get nominal temperature of the thermistor (in C).

For example:
```
CONF:SENSOR1:TEMPN?
25.0
```



### MEASure Commands
These commands are for reading (measuring) the current input/output values on Fan and Motherboard Fan ports.

#### MEASure:Read?
This command returns all measurements for all FAN and MBFAN, ans SENSOR ports.
(This is same as: Read?)

Response format:
```
mbfan<n>,"<name>",<output rpm>,<output tacho frequency>,<input pwm duty cycle>
...
fan<n>,"<name>",<input rpm>,<input tacho frequency>,<output pwm duty cycle>
...
sensor<n>,"<name>",<temperature>
```

Example:
```
MEAS:READ?
mbfan1,"mbfan1",1032,34.40,49.0
mbfan2,"mbfan2",0,0.00,0.0
mbfan3,"mbfan3",0,0.00,0.0
mbfan4,"mbfan4",0,0.00,0.0
fan1,"fan1",1032,34.40,42.4
fan2,"fan2",0,0.00,0.0
fan3,"fan3",0,0.00,0.0
fan4,"fan4",0,0.00,0.0
fan5,"fan5",0,0.00,0.0
fan6,"fan6",0,0.00,0.0
fan7,"fan7",0,0.00,0.0
fan8,"fan8",0,0.00,0.0
sensor1,"sensor1",25.0
sensor2,"sensor2",26.5
sensor3,"pico_temp",26.0
```

### MEASure:FANx Commands

#### MEASure:FANx?
Return current fan speed (RPM), tacho meter frequency (Hz), and output PWM signal duty cycle (%) for a fan.

Response format:
```
<duty_cycle>,<frequency>,<rpm>
```

Example:
```
MEAS:FAN1?
42,34.4,1032
```

#### MEASure:FANx:Read?
Return current fan speed (RPM), tacho meter frequency (Hz), and output PWM signal duty cycle (%) for a fan.

This is same as: MEASure:FANx?

Response format:
```
<duty_cycle>,<frequency>,<rpm>
```

Example:
```
MEAS:FAN1:R?
42,34.4,1032
```

#### MEASure:FANx:RPM?
Return current fan speed (RPM).

Example:
```
MEAS:FAN1:RPM?
1032
```

#### MEASure:FANx:PWM?
Return current fan (output) PWM signal duty cycle (%).

Example:
```
MEAS:FAN1:PWM?
42
```

#### MEASure:FANx:TACHo?
Return current fan tachometer (speed) signal frequency (Hz).

Example:
```
MEAS:FAN1:TACH?
34.4
```

### MEASure:MBFANx Commands

#### MEASure:MBFANx?
Return current motherboard fan connector signals. Output fan speed speed (RPM), output tacho meter frequency (Hz), and input PWM signal duty cycle (%) for a mbfan.

Response format:
```
<duty_cycle>,<frequency>,<rpm>
```

Example:
```
MEAS:MBFAN1?
49,34.4,1032
```

#### MEASure:MBFANx:Read?
Return current fan speed (RPM), tacho meter frequency (Hz), and output PWM signal duty cycle (%) for a fan.

This is same as: MEASure:MBFANx?

Response format:
```
<duty_cycle>,<frequency>,<rpm>
```

Example:
```
MEAS:MBFAN1:R?
49,34.4,1032
```

#### MEASure:MBFANx:RPM?
Return current fan speed (RPM) reported out to motherboard.

Example:
```
MEAS:MBFAN1:RPM?
1032
```

#### MEASure:MBFANx:PWM?
Return current fan (input) PWM signal duty cycle (%) received from motherboard.

Example:
```
MEAS:MBFAN1:PWM?
49
```

#### MEASure:MBFANx:TACHo?
Return current fan tachometer (speed) signal frequency (Hz) reported out to motherboard.

Example:
```
MEAS:MBFAN1:TACH?
34.4
```

### MEASure:SENSORx Commands

#### MEASure:SENSORx?
Return current temperature (C) measured by the sensor.

Example:
```
MEAS:SENSOR1?
25
```

#### MEASure:SENSORx:Read?
Return current temperature (C) measured by the sensor.

This is same as: MEASure:SENSORx?

Example:
```
MEAS:SENSOR1:R?
25
```

#### MEASure:SENSORx:TEMP?
Return current fan speed (RPM) reported out to motherboard.

This is same as: MEASure:SENSORx?

Example:
```
MEAS:SENSOR1:TEMP?
25
```

### Read Commands

#### Read?
This command returns all measurements for all FAN and MBFAN, ans SENSOR ports.
(This is same as: MEASure:Read?)

Response format:
```
mbfan<n>,"<name>",<output rpm>,<output tacho frequency>,<input pwm duty cycle>
...
fan<n>,"<name>",<input rpm>,<input tacho frequency>,<output pwm duty cycle>
...
sensor<n>,"<name>",<temperature>
```

Example:
```
MEAS:READ?
mbfan1,"mbfan1",1032,34.40,49.0
mbfan2,"mbfan2",0,0.00,0.0
mbfan3,"mbfan3",0,0.00,0.0
mbfan4,"mbfan4",0,0.00,0.0
fan1,"fan1",1032,34.40,42.4
fan2,"fan2",0,0.00,0.0
fan3,"fan3",0,0.00,0.0
fan4,"fan4",0,0.00,0.0
fan5,"fan5",0,0.00,0.0
fan6,"fan6",0,0.00,0.0
fan7,"fan7",0,0.00,0.0
fan8,"fan8",0,0.00,0.0
sensor1,"sensor1",25.0
sensor2,"sensor2",26.5
sensor3,"pico_temp",26.0
```

### SYStem Commands

#### SYStem:DEBug
Set the system debug level. This controls the logging to the console.

Default: 0   (do not log any debug messages)

Example: Enable verbose debug output
```
SYS:DEBUG 2
```

#### SYStem:DEBug?
Display current system debug level.

Example: 
```
SYS:DEBUG?
```

#### SYStem:ECHO
Enable or disaple local echo on the console.
This can be useful if interactively programming Fanpico.

Value|Status
-----|------
0|Local Echo disabled.
1|Local Echo enabled.

Default: 0 

Example: enable local echo
```
SYS:ECHO 1
```

Example: disable local echo
```
SYS:ECHO 0
```

#### SYStem:ECHO?
Display local echo status:

Example:
```
SYS:ECHO?
0
```


#### SYStem:FANS?
Display number of FAN ouput ports available.

Example:
```
SYS:FANS?
8
```


#### SYStem:LED
Set system indicator LED operating mode.

Supported modes:

mode|description
----|-----------
0   | LED blinking slowly  [default]
1   | LED on (continuously)
2   | LED off


Default: 0

Example to set LED to be on continuously:
```
SYS:LED 1
```

#### SYStem:LED?
Query current system LED operating mode.

Example:
```
SYS:LED?
0
```


#### SYStem:MBFANS?
Display number of MBFAN input ports available.

Example:
```
SYS:MBFANS?
4
```


#### SYStem:SENSORS?
Display number of (temperature) sensors available.
Last temperature sensor is the internal temperature sensor on the
RP2040.

Example:
```
SYS:SENSORS?
3
```


#### SYStem:UPGRADE
Reboot unit to USB (BOOTSEL) mode for firmware upgrade.
This command triggers fanpico to reboot and enter USB "mode", where
new firmware can simply be copied to the USB drive that appears.
After file has been copied, fanpico will automatically reboot to new
firmware image.

Example:
```
SYS:VER?
```


#### SYStem:VERsion?
Display software version and copyright information.

Example:
```
SYS:VER?
```

