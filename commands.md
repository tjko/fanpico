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
* [CONFigure:FANx:MAXpwm?](#configurefanxmaxpwm)
* [CONFigure:FANx:PWMCoeff](#configurefanxpwmcoeff)
* [CONFigure:FANx:PWMCoeff?](#configurefanxpwmcoeff)
* [CONFigure:FANx:RPMFactor](#configurefanxrpmfactor)
* [CONFigure:FANx:RPMFactor?](#configurefanxrpmfactor)
* [CONFigure:FANx:SOUrce](#configurefanxsource)
* [CONFigure:FANx:SOUrce?](#configurefanxsource)
* [CONFigure:FANx:PWMMap](#configurefanxpwmmap)
* [CONFigure:FANx:PWMMap?](#configurefanxpwmmap)
* [CONFigure:MBFANx:NAME](#configurembfanxname)
* [CONFigure:MBFANx:NAME?](#configurembfanxname)
* [CONFigure:MBFANx:MINrpm](#configurembfanxminrpm)
* [CONFigure:MBFANx:MINrpm?](#configurembfanxminrpm)
* [CONFigure:MBFANx:MAXrpm](#configurembfanxmaxrpm)
* [CONFigure:MBFANx:MAXrpm?](#configurembfanxmaxrpm)
* [CONFigure:MBFANx:RPMCoeff](#configurembfanxrpmcoeff)
* [CONFigure:MBFANx:RPMCoeff?](#configurembfanxrpmcoeff)
* [CONFigure:MBFANx:RPMFactor](#configurembfanxrpmfactor)
* [CONFigure:MBFANx:RPMFactor?](#configurembfanxrpmfactor)
* [CONFigure:MBFANx:SOUrce](#configurembfanxsource)
* [CONFigure:MBFANx:SOUrce?](#configurembfanxsource)
* [CONFigure:MBFANx:RPMMap](#configurembfanxrpmmap)
* [CONFigure:MBFANx:RPMMap?](#configurembfanxrpmmap)
* [CONFigure:SENSORx:NAME](#configuresensorxname)
* [CONFigure:SENSORx:TEMPOffset](#configuresensorxtempoffset)
* [CONFigure:SENSORx:TEMPCoeff](#configuresensorxtempcoeff)
* [CONFigure:SENSORx:TEMPMap](#configuresensorxtempmap)
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
1|MBFAN 1
2|MBFAN 2
3|MBFAN 3
4|MBFAN 4
5|MBFAN 1
6|MBFAN 2
7|MBFAN 3
8|MBFAN 4
 
Example: Set FAN 5 to follow temperature sensor 2
```
CONF:FAN5:SOURCE SENSOR 2
```

Example: Set FAN 6 to run always at fixed 50% duty cycle (speed)
```
CONF:FAN6:SOURCE FIXED 50
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


