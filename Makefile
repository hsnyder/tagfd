#-fsanitize=address doesn't work on WSL it seems..

CCFLAGS= -g -Wall -Iinclude -fsanitize=undefined

tfdconfig: src/tfdconfig.c src/tagfd-toolkit.c
	gcc src/tfdconfig.c src/tagfd-toolkit.c $(CCFLAGS) -o bin/tfdconfig
	
tfdbrowse: src/tfdbrowse.c src/tagfd-toolkit.c
	gcc src/tfdbrowse.c src/tagfd-toolkit.c $(CCFLAGS) -lncurses -o bin/tfdbrowse

tfd: src/tfd.c src/tagfd-toolkit.c
	gcc src/tfd.c src/tagfd-toolkit.c $(CCFLAGS) -o bin/tfd

tfdrelay: src/tfdrelay.c src/tagfd-toolkit.c
	gcc src/tfdrelay.c src/tagfd-toolkit.c $(CCFLAGS) -o bin/tfdrelay

tfdlog: src/tfdlog.c src/tagfd-toolkit.c
	gcc src/tfdlog.c src/tagfd-toolkit.c $(CCFLAGS) -lsqlite3 -o bin/tfdlog

# You can add -DNO_DAEMON to build controlengined as a normal application. 
controlengined: src/controlengine.c src/tagfd-toolkit.c
	gcc src/controlengine.c src/tagfd-toolkit.c $(CCFLAGS) -o bin/controlengined

rule-tempsimulator: src/rule-tempsimulator.c
	gcc src/rule-tempsimulator.c $(CCFLAGS) -lm -o bin/rule-tempsimulator
    
rule-tempcontrol: src/rule-tempcontrol.c
	gcc src/rule-tempcontrol.c $(CCFLAGS) -lm -o bin/rule-tempcontrol

rule-heatloss-sim: src/rule-heatloss-sim.c
	gcc src/rule-heatloss-sim.c $(CCFLAGS) -lm -o bin/rule-heatloss-sim

all: tfdconfig tfdbrowse tfd tfdrelay controlengined rule-tempsimulator rule-heatloss-sim rule-tempcontrol

clean:
	rm bin/*
