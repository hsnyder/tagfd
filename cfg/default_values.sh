#!/bin/bash

tfd sv master.on 1
tfd sq master.on GOOD

tfd sv houseSize.m2 2000
tfd sq houseSize.m2 GOOD

tfd sv coeff.heatloss.W_degCm2 2.17
tfd sq coeff.heatloss.W_degCm2 GOOD

tfd sv tstat.PV.degC 10

tfd sv tstat.SP.degC 21
tfd sq tstat.SP.degC GOOD

tfd sv PID.KP 8
tfd sq PID.KP GOOD
tfd sv PID.KI 8
tfd sq PID.KI GOOD
tfd sv PID.KD 3
tfd sq PID.KD GOOD