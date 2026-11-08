//
//  SSDT-ECIRQ.dsl
//  Give the embedded controller its GPIO interrupt back
//
//  Diagnosis, measured on the machine
//  ----------------------------------
//  AsusSMCEC = "crsRet=0x0 crsLen=18 irq=MISSING gpio=-1"
//
//  18 bytes is exactly the IO-only resource set: two 8-byte IO descriptors
//  plus the end tag. The other set, the one that carries the interrupt, adds
//  a GpioInt descriptor and is longer. So EC0 is being declared to macOS with
//  no interrupt at all.
//
//  Consequences, all confirmed against the live machine:
//
//    * AppleACPIEC loads (kmutil shows it) but has no interrupt source to
//      install a handler on, so EC0's _Qxx query methods never run.
//    * Every Fn key in this DSDT is delivered from a _Qxx handler calling
//      ^^^^ATKD.IANE(0x..), so none of them can ever fire.
//    * ATKD.INIT does succeed (ret=1), so ATKP is One and all the
//      "If (ATKP)" guards would pass - the switch is open, but nothing
//      ever reaches it.
//    * _WED(0xFF) always answers Ones (0xFFFFFFFF): the event queue is
//      permanently empty because the only producer, IANQ(), is only called
//      from IANE().
//    * AsusSMC receives no ACPI notification whatsoever.
//
//  Why the interrupt is missing
//  ----------------------------
//  EC0._CRS returns one of two templates:
//
//      If ((RDHW == Zero))  Return (BUF0)     // IO only, no interrupt
//      Else                 Return (BUF1)     // IO + GpioInt, pin 0x16
//
//  RDHW is a single byte in GNVS (BIOS/OS shared memory at 0xEC539998).
//  Nothing in the 11522-line DSDT ever writes it; the BIOS sets it at boot.
//  On this machine it is evidently zero.
//
//  What this table does
//  --------------------
//  Replaces EC0._CRS with a version that always returns the resource set
//  carrying the GPIO interrupt. The descriptor is copied verbatim from this
//  DSDT's own BUF1 - same pins, same edge/level and wake flags - so nothing
//  about the hardware contract changes. Only the RDHW branch is removed.
//
//  This is not a rewrite of anything the firmware does; it makes the
//  firmware's own second template the one that is always used.
//
//  If Fn keys start working, this is the fix. If nothing changes, the
//  interrupt was not the missing link and this table should be removed again.
//
//  Build: iasl -p SSDT-ECIRQ.aml SSDT-ECIRQ.dsl
//

DefinitionBlock ("", "SSDT", 2, "ASUS", "ECIRQ", 0x00000001)
{
    External (_SB_.PCI0.SBRG.EC0_, DeviceObj, NotSerialized)
    External (_SB_.GPIO, DeviceObj, NotSerialized)

    Scope (_SB_.PCI0.SBRG.EC0_)
    {
        Method (_CRS, 0, NotSerialized)
        {
            Name (RBUF, ResourceTemplate ()
            {
                IO (Decode16, 0x0062, 0x0062, 0x00, 0x01)
                IO (Decode16, 0x0066, 0x0066, 0x00, 0x01)
                GpioInt (Edge, ActiveLow, ExclusiveAndWake, PullUp, 0x0000,
                    "\\_SB.GPIO", 0x00, ResourceConsumer, ,
                    )
                    {   // Pin list
                        0x0016
                    }
            })
            Return (RBUF)
        }
    }
}
