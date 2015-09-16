#ifndef DHTNODE_H
#define DHTNODE_H


#include "headers.h"
#include "dht_world.h"

class dht_world;

class dht_node {		//add checkpoint againce to long path
  protected:

	ID_addr my_dht_address;
	phisical_addr my_phisical_address;
	map<ID_addr, list<phisical_addr> > known_dht_addresses;

	dht_world *wr;

  public:

	dht_node():my_dht_address(0) {
		;
	}
	dht_node(ID_addr addr):my_dht_address(addr) {
		;
	}
	dht_node(ID_addr addr,map<ID_addr, list<phisical_addr> > known_addresses ):my_dht_address(addr),known_dht_addresses(known_addresses) {
		;
	}

	void set_world(dht_world *world) {
		wr = world;
	}

	void set_my_dht_address(ID_addr address) {
		my_dht_address = address;
	}
	void add_dht_address(ID_addr addr,list<phisical_addr> trace) {
		known_dht_addresses.insert(pair <ID_addr,list<phisical_addr> >(addr,trace));
	}

    void asked_for_known_node(nodes_path pth);

	void send_to_phisical_addr(phisical_addr addr,trace n_trace);		// send by physic addres
	void send_to_dht_node(ID_addr,trace n_trace);						// send by hashed ID

	ID_addr get_nearest_node(ID_addr);									// finding nearest node
	void recive(trace m_trace);											// recives route package

    ID_addr get_farest_node(ID_addr);

	phisical_addr get_phisical_addr() {
		return my_phisical_address;
	}


    void send_to_phisical_addr(phisical_addr addr,nodes_path pth);

    void recived_known_node_question(nodes_path pth);


	void set_phisical_addr (phisical_addr addr) {
        my_phisical_address = addr ;
    }
    void start_tracking(ID_addr target);
};

#endif // DHTNODE_H
