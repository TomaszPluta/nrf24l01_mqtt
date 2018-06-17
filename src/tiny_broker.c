
/*
 * tiny_broker.c
 *
 *  Created on: 07.05.2018
 *      Author: tomek
 */

#include "tiny_broker.h"
#include <string.h>




#define BROKER_TIMEOUT		60
#define TOPIC_POS 			6
#define M_HEAP_SIZE			512
#define X_MALLOC				m_malloc

#define X_HTONS(a) ((a>>8) | (a<<8))

extern local_host_t local_lost;


 void broker_init (broker_t * broker, MqttNet* net){
 	memset(broker, 0, sizeof(broker_t));
 	broker->net = net;
 }


void * m_malloc(size_t size){
	static uint8_t m_heap[M_HEAP_SIZE];
	static uint16_t prev_used_bytes_nb;
	static uint16_t new_used_bytes_nb;
	prev_used_bytes_nb = new_used_bytes_nb;
	new_used_bytes_nb = prev_used_bytes_nb + size;
	if (new_used_bytes_nb < M_HEAP_SIZE){
		return &m_heap[prev_used_bytes_nb];
	}
	else{
		return NULL;
	}
}



bool is_client_connected(broker_t * broker, char* client_id){
	for (uint8_t i =0; i < MAX_CONN_CLIENTS; i++){
		if (broker->clients[i].active){
			return true;
		}
	}
	return false;
}

static inline void read_connection_flags(conn_flags_t ** conn_flags, uint8_t * frame){
	uint8_t * flag_byte = &frame[9];
	*conn_flags =  (conn_flags_t*) flag_byte;
}



static inline bool can_broker_accept_next_client(broker_t * broker){
	for (uint8_t i = 0; i < MAX_CONN_CLIENTS; i++){
		if (!(broker->clients[i].active)){
			return true;
		}
	}
	return false;
}

static inline uint8_t broker_find_client_pos(broker_t * broker, char* client_id){
	for (uint8_t i = 0; i < MAX_CONN_CLIENTS; i++){
		if (strcmp(broker->clients[i].id, client_id) ==0 ) {
			return i;
		}
	}

	return NOT_FOUND;

}

bool broker_remove_client(broker_t * broker, char* client_id){
	uint8_t pos = broker_find_client_pos(broker, client_id);
	if (pos != NOT_FOUND){
		broker->clients[pos].active = false;
		return true;
	}
	return false;
}


static uint8_t broker_first_free_pos_for_client(broker_t * broker){
	for (uint8_t i = 0; i < MAX_CONN_CLIENTS; i++){
		if (!(broker->clients[i].active)){
			return i;
		}
	}
	return NOT_FOUND;
}


static void add_client (broker_t * broker, conn_client_t * new_client){
	uint8_t pos = broker_first_free_pos_for_client(broker);
		memcpy(&broker->clients[pos], new_client, sizeof (conn_client_t));
}

//static conn_client_t * get_free_slot_for_client(broker_t * broker){
//	for (uint8_t i = 0; i < MAX_CONN_CLIENTS; i++){
//		if (!(broker->clients[i].active)){
//			return &broker->clients[i];
//		}
//	}
//	return NULL;
//}
//


/*-------------------------------PUBLIHS-----------------------------------------*/


static inline void read_conn_header(conn_header_t *header, uint8_t * frame){
	uint8_t pos = 0;
	header->fix_head =  (conn_fixed_header_t *) &frame[pos];
	pos += 2;

	header->len = (uint16_t*) &frame[pos];
	*header->len = X_HTONS(*header->len);
	pos += 2;

	header->proto_name = (char*) &frame[pos];
	pos += *header->len;

	header->proto_level = (uint8_t*) &frame[pos];
	pos += 1;

	header->conn_flags = (conn_flags_t*) &frame[pos];
	pos += 1;

	header->keep_alive = (uint16_t*)  &frame[pos];
	*header->keep_alive = X_HTONS(*header->keep_alive);

}





static inline void read_conn_payload(conn_pld_t *payload, conn_header_t* header, uint8_t* frame ){
	uint8_t pos = PLD_START;

	payload->client_id_len  = (uint16_t*) &frame[pos];
	*payload->client_id_len = X_HTONS(*payload->client_id_len);
	pos += 2;
	payload->client_id = (char*) &frame[pos];
	pos += *payload->client_id_len;


	if (header->conn_flags->last_will){

		payload->will_topic_len = (uint16_t*)  &frame[pos];
		*payload->will_topic_len = X_HTONS(* payload->will_topic_len);
		pos += 2;
		payload->will_topic = (char*)  &frame[pos];
		pos += *payload->will_topic_len;

		payload->will_msg_len = (uint16_t*)  &frame[pos];
		*payload->will_msg_len = X_HTONS(* payload->will_msg_len);
		pos += 2;
		payload->will_msg = (char*)  &frame[pos];
		pos += *payload->will_msg_len;
	}
	if (header->conn_flags->user_name){
		payload->usr_name_len = (uint16_t*)  &frame[pos];
		*payload->usr_name_len = X_HTONS(* payload->usr_name_len);
		pos += 2;
		payload->usr_name= (char*) &frame[pos];
		pos += *payload->usr_name_len;
	}
	if (header->conn_flags->pswd){
		payload->pswd_len = (uint16_t*)  &frame[pos];
		*payload->pswd_len = X_HTONS(* payload->pswd_len);
		pos += 2;
		payload->pswd= (char*) &frame[pos];
		pos += *payload->pswd_len;
	}

}


bool is_client_authorised(char* usr_name, char* pswd){
	return true;
}


uint8_t * format_conn_ack(header_conn_ack_t * header_ack, bool session_pres, uint8_t code){
	memset(header_ack, 0, sizeof (header_conn_ack_t));
	header_ack->control_type = (CONTR_TYPE_CONNACK << 4);
	header_ack->remainin_len = CONN_ACK_PLD_LEN;
	header_ack->ack_flags.session_pres = session_pres;
	header_ack->conn_code = code;
	return (uint8_t *)header_ack;
}



void broker_fill_new_client(conn_client_t *new_client, const conn_pck_t * conn_pck, uint8_t* net_address){
	conn_header_t * header = conn_pck->head;
	conn_flags_t * flags = header->conn_flags;
	conn_pld_t * payload = conn_pck->pld;

	new_client->id = X_MALLOC((*payload->client_id_len)+1);
	strncpy(new_client->id,  payload->client_id, *payload->client_id_len);

	new_client->keepalive = *header->keep_alive;

	if (flags->will_retain){
		new_client->will_retain = 1;
	}

	if (flags->last_will){
		new_client->will_retain = 1;
		new_client->will_topic = X_MALLOC((*payload->will_topic_len)+1);
		strncpy(new_client->will_topic,  payload->will_topic, *payload->will_topic_len );

		new_client->will_msg = X_MALLOC((*payload->will_msg_len)+1);
		strncpy(new_client->will_msg,  payload->will_msg, *payload->will_msg_len);

		new_client->will_qos = flags->will_qos;

	}

	if (flags->user_name){
		new_client->username = X_MALLOC((*payload->usr_name_len)+1);
		strncpy(new_client->username,  payload->usr_name, *payload->usr_name_len);
	}

	if (flags->pswd){
		new_client->password = X_MALLOC((*payload->pswd_len)+1);
		strncpy(new_client->password,  payload->pswd, *payload->pswd_len);
	}
}



// https://www.bevywise.com/developing-mqtt-clients/
// https://morphuslabs.com/hacking-the-iot-with-mqtt-8edaf0d07b9b ack codes



void broker_decode_connect_frame (broker_t * broker, uint8_t * frame, conn_pck_t * conn_pck ){
	read_conn_header(conn_pck->head, frame);
	read_conn_payload(conn_pck->pld, conn_pck->head, frame);
}



void broker_mantain_new_connect (broker_t *broker, conn_pck_t *conn_pck, conn_ack_stat_t * stat, uint8_t* net_add){

	if  (*conn_pck->head->proto_level != PROTO_LEVEL_MQTT311){
		stat->session_present = false;
		stat->code = CONN_ACK_BAD_PROTO;
		return;
	}

	if (conn_pck->head->conn_flags->cleans_session){
		if (broker_remove_client(broker, conn_pck->pld->client_id)){
		}
	}

	if (is_client_connected(broker, conn_pck->pld->client_id)){
		stat->session_present = true;
		stat->code = CONN_ACK_OK;
		return;
	}

	if (can_broker_accept_next_client(broker))
	{
		conn_client_t new_client;
		broker_fill_new_client(&new_client, conn_pck, net_add);

		if (is_client_authorised(new_client.username, new_client.password)){
			add_client(broker, &new_client);
			stat->session_present = false;
			stat->code = CONN_ACK_OK;
			return;

		}else{
			stat->session_present = false;
			stat->code = CONN_ACK_BAD_AUTH;
			return;
		}
	} else {
		stat->session_present = false;
		stat->code = CONN_ACK_NOT_AVBL;
		return;
	}
}


void broker_send_conn_ack(broker_t * broker,  conn_ack_stat_t * stat){
	header_conn_ack_t header_ack;
	format_conn_ack(&header_ack, stat->session_present, stat->code);
	uint8_t * buf = (uint8_t *) &header_ack;
	uint8_t buf_len = sizeof(header_conn_ack_t);
	broker->net->write(NULL, buf, buf_len, DEFAULT_BROKER_TIMEOUT);

}


/*-------------------------------PUBLIHS-----------------------------------------*/

 void broker_decode_publish(uint8_t* frame, pub_pck_t * pub_pck){
	uint8_t pos = 0;

	pub_pck->fix_head.ctrl_byte = (pub_ctrl_byte_t *) frame;
	pos ++;
	pub_pck->fix_head.rem_len = decode_pck_len(&frame[pos]);
	pos ++;


	pub_pck->var_head.topic_name_len  = (uint16_t*) &frame[pos];
	*pub_pck->var_head.topic_name_len = X_HTONS(*pub_pck->var_head.topic_name_len);
	pos += 2;

	pub_pck->var_head.topic_name = (unsigned char*)  &frame[pos];
	pos += *pub_pck->var_head.topic_name_len;

	if (pub_pck->fix_head.ctrl_byte->QoS > 0){
		pub_pck->var_head.packet_id  = (uint16_t*) &frame[pos];
		*pub_pck->var_head.packet_id = X_HTONS(*pub_pck->var_head.packet_id);
		pos += 2;
	}
	pub_pck->pld = &frame[pos];
}




void publish_msg_to_subscribers(broker_t * broker, pub_pck_t * pub_pck){
	for (uint8_t i =0; i < MAX_CONN_CLIENTS; i++){
		if ((broker->clients[i].active)){
			for (uint8_t j =0; j < MAX_SUBS_TOPIC; j++){
				uint16_t len = *pub_pck->var_head.topic_name_len;
				unsigned char* topic = pub_pck->var_head.topic_name;
				if (memcmp (&broker->clients[i].subs_topic[j].topic_name, topic, len)){
					broker->net->write(broker->clients[i].net_address, topic, len, BROKER_TIMEOUT);
					break;
				}
			}
		}
	}
}



uint32_t decode_pck_len (uint8_t * frame){
	uint8_t multiplier = 1;
	uint32_t value = 0;
	uint8_t i=0;
	uint8_t  encodedByte;
	const uint8_t max_nb_bytes_rl = 4;
	do{
		encodedByte = frame[i];
		value += (encodedByte & 127) * multiplier;
		multiplier *= 128;
		i++;
		if (i == max_nb_bytes_rl){
			break;
		}
	}while ((encodedByte & 128) != 0);
	return value;
}




/*-------------------------------SUBSCRIBE-----------------------------------------*/

void broker_decode_subscribe(uint8_t* frame, sub_pck_t * sub_pck){
	uint8_t pos = 0;

	sub_pck->fix_head.subs_ctrl_byte = (subs_ctrl_byte_t *) frame;
	pos++;
	sub_pck->fix_head.rem_len = decode_pck_len(&frame[pos]);
	pos ++;

	sub_pck->var_head.packet_id  = (uint16_t*) &frame[pos];
	*sub_pck->var_head.packet_id = X_HTONS(*sub_pck->var_head.packet_id);
	pos += 2;

	const uint8_t fix_head_size = 2;
	uint8_t topic_nb =0;
	while (pos < (sub_pck->fix_head.rem_len + fix_head_size)){
		sub_pck->pld_topics[topic_nb].topic_name_len = (uint16_t *)  &frame[pos];
		*sub_pck->pld_topics[topic_nb].topic_name_len  = X_HTONS(*sub_pck->pld_topics[topic_nb].topic_name_len );
		pos += 2;
		sub_pck->pld_topics[topic_nb].topic_name =  (unsigned char*)  &frame[pos];
		pos += (*sub_pck->pld_topics[topic_nb].topic_name_len);
		sub_pck->pld_topics[topic_nb].qos = (uint8_t*) &frame[pos];
		pos += 1;
		topic_nb++;
	}
}



/*TODO: extract methods (and len value)*/
void add_subscribtion(conn_client_t *client, sub_pck_t * sub_pck){
	for (uint8_t i=0; i < MAX_SUBS_TOPIC; i++){
		for (uint8_t j =0; j < MAX_SUBS_TOPIC; j++){
			/*to be sure that one topic is not subset of another filter*/
			if (client->subs_topic[i].topic_name != NULL){
				if (*client->subs_topic[i].topic_name_len == *sub_pck->pld_topics[j].topic_name_len){
					if (memcmp(client->subs_topic[i].topic_name, sub_pck->pld_topics[j].topic_name, *sub_pck->pld_topics[j].topic_name_len)){
						client->subs_topic[i].qos = sub_pck->pld_topics[j].qos;
					}
				}
			}else{
				memcpy(&client->subs_topic[i], &sub_pck->pld_topics[j], *sub_pck->pld_topics[j].topic_name_len);
				break;
			}
		}
	}
}






//broker->net->write(context, buf, buf_len, timeout_ms);
//broker->net->write(void *context, const byte* buf, int buf_len, int timeout_ms);

