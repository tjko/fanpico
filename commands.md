# Fanpico: Command Reference
Fanpico uses "SCPI like" command set. It does not implement full SCPI command set, but is mostly compatible from programming point of view.

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
Identify device. This returns string that contains following fields: <manufacturer>,<model number>,<serial number>,<firmware version>
  
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
CONFIGURE:FAN1:NAME CPU Fan
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


  
