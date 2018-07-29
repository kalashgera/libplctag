/***************************************************************************
 *   Copyright (C) 2015 by OmanTek                                         *
 *   Author Kyle Hayes  kylehayes@omantek.com                              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

/**************************************************************************
 * CHANGE LOG                                                             *
 *                                                                        *
 * 2012-02-23  KRH - Created file.                                        *
 *                                                                        *
 * 2012-06-15  KRH - Rename file and includes for code re-org to get      *
 *                   ready for DF1 implementation.  Refactor some common  *
 *                   parts into ab/util.c.                                *
 *                                                                        *
 * 2012-06-20  KRH - change plc_err() calls for new error API.            *
 *                                                                        *
 * 2012-12-19   KRH - Start refactoring for threaded API and attributes.   *
 *                                                                        *
 **************************************************************************/

#include <ctype.h>
#include <platform.h>
#include <lib/libplctag.h>
#include <lib/tag.h>
#include <ab/defs.h>
#include <ab/ab_common.h>
#include <ab/cip.h>
#include <ab/tag.h>
#include <ab/session.h>
#include <ab/eip_cip.h>
#include <ab/error_codes.h>
#include <util/attr.h>
#include <util/debug.h>
#include <util/vector.h>


//int allocate_request_slot(ab_tag_p tag);
//int allocate_read_request_slot(ab_tag_p tag);
//int allocate_write_request_slot(ab_tag_p tag);
//int multi_tag_read_start(ab_tag_p tag);
static int build_read_request_connected(ab_tag_p tag, int byte_offset);
static int build_read_request_unconnected(ab_tag_p tag, int byte_offset);
static int build_write_request_connected(ab_tag_p tag, int byte_offset);
static int build_write_request_unconnected(ab_tag_p tag, int byte_offset);
static int check_read_status_connected(ab_tag_p tag);
static int check_read_status_unconnected(ab_tag_p tag);
static int check_write_status_connected(ab_tag_p tag);
static int check_write_status_unconnected(ab_tag_p tag);
//int calculate_write_sizes(ab_tag_p tag);
static int calculate_write_data_per_packet(ab_tag_p tag);

/*
    tag_vtable_func abort;
    tag_vtable_func read;
    tag_vtable_func status;
    tag_vtable_func tickler;
    tag_vtable_func write;
*/

static int tag_read_start(ab_tag_p tag);
static int tag_status(ab_tag_p tag);
static int tag_tickler(ab_tag_p tag);
static int tag_write_start(ab_tag_p tag);

/* define the exported vtable for this tag type. */
struct tag_vtable_t eip_cip_vtable = {
    (tag_vtable_func)ab_tag_abort, /* shared */
    (tag_vtable_func)tag_read_start,
    (tag_vtable_func)tag_status,
    (tag_vtable_func)tag_tickler,
    (tag_vtable_func)tag_write_start
};


/*************************************************************************
 **************************** API Functions ******************************
 ************************************************************************/

/*
 * tag_status
 *
 * CIP-specific status.  This functions as a "tickler" routine
 * to check on the completion of async requests.
 */
int tag_status(ab_tag_p tag)
{
    int rc = PLCTAG_STATUS_OK;
    int session_rc = PLCTAG_STATUS_OK;

    if (tag->read_in_progress) {
        return PLCTAG_STATUS_PENDING;
    }
//        if(tag->connection) {
//            rc = check_read_status_connected(tag);
//        } else {
//            rc = check_read_status_unconnected(tag);
//        }
//
//        return rc;
//    }
//
    if (tag->write_in_progress) {
        return PLCTAG_STATUS_PENDING;
    }
//        if(tag->connection) {
//            rc = check_write_status_connected(tag);
//        } else {
//            rc = check_write_status_unconnected(tag);
//        }
//
//        return rc;
//    }

    if (tag->session) {
        session_rc = tag->session->status;
    } else {
        /* this is not OK.  This is fatal! */
        session_rc = PLCTAG_ERR_CREATE;
    }

    /* now collect the status.  Highest level wins. */
    rc = session_rc;

    if(rc == PLCTAG_STATUS_OK) {
        rc = tag->status;
    }

    return rc;
}



int tag_tickler(ab_tag_p tag)
{
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_DETAIL,"Starting.");

    if (tag->read_in_progress) {
        if(tag->use_connected_msg) {
            rc = check_read_status_connected(tag);
        } else {
            rc = check_read_status_unconnected(tag);
        }

        pdebug(DEBUG_DETAIL,"Done.  Read in progress.");

        return rc;
    }

    if (tag->write_in_progress) {
        if(tag->use_connected_msg) {
            rc = check_write_status_connected(tag);
        } else {
            rc = check_write_status_unconnected(tag);
        }

        pdebug(DEBUG_DETAIL, "Done. Write in progress.");

        return rc;
    }

    pdebug(DEBUG_DETAIL, "Done.  No operation in progress.");

    return tag->status;
}





/*
 * tag_read_start
 *
 * This function must be called only from within one thread, or while
 * the tag's mutex is locked.
 *
 * The function starts the process of getting tag data from the PLC.
 */

int tag_read_start(ab_tag_p tag)
{
    int rc = PLCTAG_STATUS_OK;
//    int i;
//    int byte_offset = 0;

    pdebug(DEBUG_INFO, "Starting");

//    if(tag->read_group) {
//        pdebug(DEBUG_DETAIL,"Redirecting to the multi-tag read code.");
//        return multi_tag_read_start(tag);
//    }

    /* is this the first read? */
//    if (1 || tag->first_read) {
    /*
     * On a new tag, the first time we read, we go through and
     * request the maximum possible (up to the size of the tag)
     * each time.  We record what we actually get back in the
     * tag->read_req_sizes array.  The next time we read, we
     * use that array to make the new requests.
     */

//        rc = allocate_read_request_slot(tag);
//
//        if (rc != PLCTAG_STATUS_OK) {
//            pdebug(DEBUG_WARN,"Unable to allocate read request slot!");
//            return rc;
//        }

    /*
     * The PLC may not send back as much data as we would like.
     * So, we attempt to determine what the size will be by
     * single-stepping through the requests the first time.
     * This will be slow, but subsequent reads will be pipelined.
     */

    /* determine the byte offset this time. */
//    tag->byte_offset = 0;

//        /* scan and add the byte offsets */
//        for (i = 0; i < tag->num_read_requests && tag->reqs[i]; i++) {
//            byte_offset += tag->read_req_sizes[i];
//        }
//
//        pdebug(DEBUG_DETAIL, "First read tag->num_read_requests=%d, byte_offset=%d.", tag->num_read_requests, byte_offset);

    /* mark the tag read in progress */
    tag->read_in_progress = 1;

    /* i is the index of the first new request */
    if(tag->use_connected_msg) {
        rc = build_read_request_connected(tag, tag->byte_offset);
    } else {
        rc = build_read_request_unconnected(tag, tag->byte_offset);
    }

    if (rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to build read request!");

        return rc;
    }

//    }

//    else {
//        /* this is not the first read, so just set up all the requests at once. */
//        byte_offset = 0;
//
//        for (i = 0; i < tag->num_read_requests; i++) {
//            if(tag->connection) {
//                rc = build_read_request_connected(tag, i, byte_offset);
//            } else {
//                rc = build_read_request_unconnected(tag, i, byte_offset);
//            }
//
//            if (rc != PLCTAG_STATUS_OK) {
//                pdebug(DEBUG_WARN,"Unable to build read request!");
//                return rc;
//            }
//
//            byte_offset += tag->read_req_sizes[i];
//        }
//    }

    pdebug(DEBUG_INFO, "Done.");

    return PLCTAG_STATUS_PENDING;
}


//int multi_tag_read_start(ab_tag_p tag)
//{
//    int rc = PLCTAG_STATUS_OK;
//    int i;
//    int byte_offset = 0;
//    vector_p read_tags = NULL;
//
//    pdebug(DEBUG_INFO, "Starting");
//
//    pdebug(DEBUG_DETAIL,"Getting read group tags for group %s.", tag->read_group);
//
//    read_tags = find_read_group_tags(tag);
//    if(!read_tags || vector_length(read_tags) == 0) {
//        pdebug(DEBUG_WARN,"Something wrong, no tags found for read group %s.",tag->read_group);
//        vector_destroy(read_tags);
//        return PLCTAG_ERR_BAD_DATA;
//    }
//
//    /* now these cannot go away since we are holding the ref. */
//    pdebug(DEBUG_DETAIL,"Found %d tags in read group %s.",vector_length(read_tags), tag->read_group);
//
//
//
//    /* is this the first read? */
//    if (1 || tag->first_read) {
//        /*
//         * On a new tag, the first time we read, we go through and
//         * request the maximum possible (up to the size of the tag)
//         * each time.  We record what we actually get back in the
//         * tag->read_req_sizes array.  The next time we read, we
//         * use that array to make the new requests.
//         */
//
//        rc = allocate_read_request_slot(tag);
//
//        if (rc != PLCTAG_STATUS_OK) {
//            pdebug(DEBUG_WARN,"Unable to allocate read request slot!");
//            return rc;
//        }
//
//        /*
//         * The PLC may not send back as much data as we would like.
//         * So, we attempt to determine what the size will be by
//         * single-stepping through the requests the first time.
//         * This will be slow, but subsequent reads will be pipelined.
//         */
//
//        /* determine the byte offset this time. */
//        byte_offset = 0;
//
//        /* scan and add the byte offsets */
//        for (i = 0; i < tag->num_read_requests && tag->reqs[i]; i++) {
//            byte_offset += tag->read_req_sizes[i];
//        }
//
//        pdebug(DEBUG_DETAIL, "First read tag->num_read_requests=%d, byte_offset=%d.", tag->num_read_requests, byte_offset);
//
//        /* i is the index of the first new request */
//        if(tag->connection) {
//            rc = build_read_request_connected(tag, i, byte_offset);
//        } else {
//            rc = build_read_request_unconnected(tag, i, byte_offset);
//        }
//
//        if (rc != PLCTAG_STATUS_OK) {
//            pdebug(DEBUG_WARN,"Unable to build read request!");
//            return rc;
//        }
//
//    } else {
//        /* this is not the first read, so just set up all the requests at once. */
//        byte_offset = 0;
//
//        for (i = 0; i < tag->num_read_requests; i++) {
//            if(tag->connection) {
//                rc = build_read_request_connected(tag, i, byte_offset);
//            } else {
//                rc = build_read_request_unconnected(tag, i, byte_offset);
//            }
//
//            if (rc != PLCTAG_STATUS_OK) {
//                pdebug(DEBUG_WARN,"Unable to build read request!");
//                return rc;
//            }
//
//            byte_offset += tag->read_req_sizes[i];
//        }
//    }
//
//    /* mark the tag read in progress */
//    tag->read_in_progress = 1;
//
//    pdebug(DEBUG_INFO, "Done.");
//
//    return PLCTAG_STATUS_PENDING;
//}


/*
 * tag_write_start
 *
 * This must be called from one thread alone, or while the tag mutex is
 * locked.
 *
 * The routine starts the process of writing to a tag.
 */

int tag_write_start(ab_tag_p tag)
{
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_INFO, "Starting");

    /*
     * if the tag has not been read yet, read it.
     *
     * This gets the type data and sets up the request
     * buffers.
     */

    if (tag->first_read) {
        pdebug(DEBUG_DETAIL, "No read has completed yet, doing pre-read to get type information.");

        tag->pre_write_read = 1;

        return tag_read_start(tag);
    }

    if (rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to calculate write sizes!");
        return rc;
    }

    /* the write is now pending */
    tag->write_in_progress = 1;

    if(tag->use_connected_msg) {
        rc = build_write_request_connected(tag, tag->byte_offset);
    } else {
        rc = build_write_request_unconnected(tag, tag->byte_offset);
    }

    if (rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to build write request!");
        return rc;
    }

    pdebug(DEBUG_INFO, "Done.");

    return PLCTAG_STATUS_PENDING;
}



int build_read_request_connected(ab_tag_p tag, int byte_offset)
{
    eip_cip_co_req* cip = NULL;
    uint8_t* data = NULL;
    ab_request_p req = NULL;
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_INFO, "Starting.");

    /* get a request buffer */
    rc = request_create(&req, tag->session->max_payload_size);
    if (rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR, "Unable to get new request.  rc=%d", rc);
        return rc;
    }

    /* point the request struct at the buffer */
    cip = (eip_cip_co_req*)(req->data);

    /* point to the end of the struct */
    data = (req->data) + sizeof(eip_cip_co_req);

    /*
     * set up the embedded CIP read packet
     * The format is:
     *
     * uint8_t cmd
     * LLA formatted name
     * uint16_t # of elements to read
     */

    //embed_start = data;

    /* set up the CIP Read request */
    *data = AB_EIP_CMD_CIP_READ_FRAG;
    data++;

    /* copy the tag name into the request */
    mem_copy(data, tag->encoded_name, tag->encoded_name_size);
    data += tag->encoded_name_size;

    /* add the count of elements to read. */
    *((uint16_le*)data) = h2le16((uint16_t)(tag->elem_count));
    data += sizeof(uint16_le);

    /* add the byte offset for this request */
    *((uint32_le*)data) = h2le32((uint32_t)byte_offset);
    data += sizeof(uint32_le);

    /* now we go back and fill in the fields of the static part */

    /* encap fields */
    cip->encap_command = h2le16(AB_EIP_CONNECTED_SEND); /* ALWAYS 0x0070 Unconnected Send*/

    /* router timeout */
    cip->router_timeout = h2le16(1); /* one second timeout, enough? */

    /* Common Packet Format fields for unconnected send. */
    cip->cpf_item_count = h2le16(2);                 /* ALWAYS 2 */
    cip->cpf_cai_item_type = h2le16(AB_EIP_ITEM_CAI);/* ALWAYS 0x00A1 connected address item */
    cip->cpf_cai_item_length = h2le16(4);            /* ALWAYS 4, size of connection ID*/
    cip->cpf_cdi_item_type = h2le16(AB_EIP_ITEM_CDI);/* ALWAYS 0x00B1 - connected Data Item */
    cip->cpf_cdi_item_length = h2le16((uint16_t)(data - (uint8_t*)(&cip->cpf_conn_seq_num))); /* REQ: fill in with length of remaining data. */

    /* set the size of the request */
    req->request_size = (int)(data - (req->data));

    /* store the connection */
//    req->connection = tag->connection;

    req->session = tag->session;

    if(tag->allow_packing) {
        request_allow_packing(req);
    }

    /* mark it as ready to send */
    req->send_request = 1;

    /* this request is connected, so it needs the session exclusively */
    req->connected_request = 1;

    /* add the request to the session's list. */
    rc = session_add_request(tag->session, req);

    if (rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR, "Unable to add request to session! rc=%d", rc);
//        request_release(req);
//        tag->reqs[slot] = rc_dec(req);
        tag->req = rc_dec(req);
        return rc;
    }

    /* save the request for later */
//    tag->reqs[slot] = req;
    tag->req = req;

    pdebug(DEBUG_INFO, "Done");

    return PLCTAG_STATUS_OK;
}



int build_read_request_unconnected(ab_tag_p tag, int byte_offset)
{
    eip_cip_uc_req* cip;
    uint8_t* data;
    uint8_t* embed_start, *embed_end;
    ab_request_p req = NULL;
    int rc;

    pdebug(DEBUG_INFO, "Starting.");

    /* get a request buffer */
    rc = request_create(&req, MAX_CIP_MSG_SIZE, tag);

    if (rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR, "Unable to get new request.  rc=%d", rc);
        return rc;
    }

//    req->num_retries_left = tag->num_retries;
//    req->retry_interval = tag->default_retry_interval;

    /* point the request struct at the buffer */
    cip = (eip_cip_uc_req*)(req->data);

    /* point to the end of the struct */
    data = (req->data) + sizeof(eip_cip_uc_req);

    /*
     * set up the embedded CIP read packet
     * The format is:
     *
     * uint8_t cmd
     * LLA formatted name
     * uint16_t # of elements to read
     */

    embed_start = data;

    /* set up the CIP Read request */
    *data = AB_EIP_CMD_CIP_READ_FRAG;
    data++;

    /* copy the tag name into the request */
    mem_copy(data, tag->encoded_name, tag->encoded_name_size);
    data += tag->encoded_name_size;

    /* add the count of elements to read. */
    /* FIXME BUG - this may not work on some processors! */
    *((uint16_le*)data) = h2le16((uint16_t)(tag->elem_count));
    data += sizeof(uint16_le);

    /* add the byte offset for this request */
    /* FIXME BUG - this may not work on some processors! */
    *((uint32_le*)data) = h2le32((uint32_t)byte_offset);
    data += sizeof(uint32_le);

    /* mark the end of the embedded packet */
    embed_end = data;

    /* Now copy in the routing information for the embedded message */
    /*
     * routing information.  Format:
     *
     * uint8_t path_size in 16-bit words
     * uint8_t reserved/pad (zero)
     * uint8_t[...] path (padded to even number of bytes)
     */
    if(tag->conn_path_size > 0) {
        *data = (tag->conn_path_size) / 2; /* in 16-bit words */
        data++;
        *data = 0; /* reserved/pad */
        data++;
        mem_copy(data, tag->conn_path, tag->conn_path_size);
        data += tag->conn_path_size;
    }

    /* now we go back and fill in the fields of the static part */

    /* encap fields */
    cip->encap_command = h2le16(AB_EIP_READ_RR_DATA); /* ALWAYS 0x0070 Unconnected Send*/

    /* router timeout */
    cip->router_timeout = h2le16(1); /* one second timeout, enough? */

    /* Common Packet Format fields for unconnected send. */
    cip->cpf_item_count = h2le16(2);                  /* ALWAYS 2 */
    cip->cpf_nai_item_type = h2le16(AB_EIP_ITEM_NAI); /* ALWAYS 0 */
    cip->cpf_nai_item_length = h2le16(0);             /* ALWAYS 0 */
    cip->cpf_udi_item_type = h2le16(AB_EIP_ITEM_UDI); /* ALWAYS 0x00B2 - Unconnected Data Item */
    cip->cpf_udi_item_length = h2le16((uint16_t)(data - (uint8_t*)(&cip->cm_service_code))); /* REQ: fill in with length of remaining data. */

    /* CM Service Request - Connection Manager */
    cip->cm_service_code = AB_EIP_CMD_UNCONNECTED_SEND; /* 0x52 Unconnected Send */
    cip->cm_req_path_size = 2;                          /* 2, size in 16-bit words of path, next field */
    cip->cm_req_path[0] = 0x20;                         /* class */
    cip->cm_req_path[1] = 0x06;                         /* Connection Manager */
    cip->cm_req_path[2] = 0x24;                         /* instance */
    cip->cm_req_path[3] = 0x01;                         /* instance 1 */

    /* Unconnected send needs timeout information */
    cip->secs_per_tick = AB_EIP_SECS_PER_TICK; /* seconds per tick */
    cip->timeout_ticks = AB_EIP_TIMEOUT_TICKS; /* timeout = src_secs_per_tick * src_timeout_ticks */

    /* size of embedded packet */
    cip->uc_cmd_length = h2le16((uint16_t)(embed_end - embed_start));

    /* set the size of the request */
    req->request_size = (int)(data - (req->data));

    /* mark it as ready to send */
    req->send_request = 1;

    if(tag->allow_packing) {
        request_allow_packing(req);
    }

    /* add the request to the session's list. */
    rc = session_add_request(tag->session, req);

    if (rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR, "Unable to add request to session! rc=%d", rc);
//        request_release(req);
//        tag->reqs[slot] = rc_dec(req);
        tag->req = rc_dec(req);
        return rc;
    }

    /* save the request for later */
//    tag->reqs[slot] = req;
    tag->req = req;

    pdebug(DEBUG_INFO, "Done");

    return PLCTAG_STATUS_OK;
}




int build_write_request_connected(ab_tag_p tag, int byte_offset)
{
    int rc = PLCTAG_STATUS_OK;
    eip_cip_co_req* cip = NULL;
    uint8_t* data = NULL;
    ab_request_p req = NULL;
    int multiple_requests = 0;
    int write_size = 0;

    pdebug(DEBUG_INFO, "Starting.");

    /* get a request buffer */
    rc = request_create(&req, tag->session->max_payload_size);
    if (rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR, "Unable to get new request.  rc=%d", rc);
        return rc;
    }

    if(tag->write_data_per_packet == 0) {
        /* FIXME - check return value! */
        calculate_write_data_per_packet(tag);
    }

    if(tag->write_data_per_packet < tag->size) {
        multiple_requests = 1;
    }

    cip = (eip_cip_co_req*)(req->data);

    /* point to the end of the struct */
    data = (req->data) + sizeof(eip_cip_co_req);

    /*
     * set up the embedded CIP read packet
     * The format is:
     *
     * uint8_t cmd
     * LLA formatted name
     * data type to write
     * uint16_t # of elements to write
     * data to write
     */

    /*
     * set up the CIP Read request type.
     * Different if more than one request.
     *
     * This handles a bug where attempting fragmented requests
     * does not appear to work with a single boolean.
     */
    *data = (multiple_requests) ? AB_EIP_CMD_CIP_WRITE_FRAG : AB_EIP_CMD_CIP_WRITE;
    data++;

    /* copy the tag name into the request */
    mem_copy(data, tag->encoded_name, tag->encoded_name_size);
    data += tag->encoded_name_size;

    /* copy encoded type info */
    if (tag->encoded_type_info_size) {
        mem_copy(data, tag->encoded_type_info, tag->encoded_type_info_size);
        data += tag->encoded_type_info_size;
    } else {
        pdebug(DEBUG_WARN,"Data type unsupported!");
        return PLCTAG_ERR_UNSUPPORTED;
    }

    /* copy the item count, little endian */
    *((uint16_le*)data) = h2le16((uint16_t)(tag->elem_count));
    data += sizeof(uint16_le);

    if (multiple_requests) {
        /* put in the byte offset */
        *((uint32_le*)data) = h2le32((uint32_t)(byte_offset));
        data += sizeof(uint32_le);
    }

    /* how much data to write? */
    write_size = tag->size - tag->byte_offset;

    if(write_size > tag->write_data_per_packet) {
        write_size = tag->write_data_per_packet;
    }

    /* now copy the data to write */
    mem_copy(data, tag->data + tag->byte_offset, write_size);
    data += write_size;
    tag->byte_offset += write_size;

    /* need to pad data to multiple of 16-bits */
    if (write_size & 0x01) {
        *data = 0;
        data++;
    }

    /* now we go back and fill in the fields of the static part */

    /* encap fields */
    cip->encap_command = h2le16(AB_EIP_CONNECTED_SEND); /* ALWAYS 0x0070 Unconnected Send*/

    /* router timeout */
    cip->router_timeout = h2le16(1); /* one second timeout, enough? */

    /* Common Packet Format fields for unconnected send. */
    cip->cpf_item_count = h2le16(2);                 /* ALWAYS 2 */
    cip->cpf_cai_item_type = h2le16(AB_EIP_ITEM_CAI);/* ALWAYS 0x00A1 connected address item */
    cip->cpf_cai_item_length = h2le16(4);            /* ALWAYS 4, size of connection ID*/
    cip->cpf_cdi_item_type = h2le16(AB_EIP_ITEM_CDI);/* ALWAYS 0x00B1 - connected Data Item */
    cip->cpf_cdi_item_length = h2le16((uint16_t)(data - (uint8_t*)(&cip->cpf_conn_seq_num))); /* REQ: fill in with length of remaining data. */

    /* set the size of the request */
    req->request_size = (int)(data - (req->data));

    /* mark it as ready to send */
    req->send_request = 1;

    /* store the connection */
//    req->connection = tag->connection;

    /* mark the request as a connected request */
    req->connected_request = 1;

    if(tag->allow_packing) {
        request_allow_packing(req);
    }

    /* add the request to the session's list. */
    rc = session_add_request(tag->session, req);

    if (rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR, "Unable to add request to session! rc=%d", rc);
//        request_release(req);
//        tag->reqs[slot] = rc_dec(req);
        tag->req = rc_dec(req);
        return rc;
    }

    /* save the request for later */
//    tag->reqs[slot] = req;
    tag->req = req;

    pdebug(DEBUG_INFO, "Done");

    return PLCTAG_STATUS_OK;
}




int build_write_request_unconnected(ab_tag_p tag, int byte_offset)
{
    int rc = PLCTAG_STATUS_OK;
    eip_cip_uc_req* cip = NULL;
    uint8_t* data = NULL;
    uint8_t *embed_start = NULL;
    uint8_t *embed_end = NULL;
    ab_request_p req = NULL;
    int multiple_requests = 0;
    int write_size = 0;

    pdebug(DEBUG_INFO, "Starting.");

    /* get a request buffer */
    rc = request_create(&req, tag->session->max_payload_size);
    if (rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR, "Unable to get new request.  rc=%d", rc);
        return rc;
    }

    if(tag->write_data_per_packet == 0) {
        /* FIXME - check return value! */
        calculate_write_data_per_packet(tag);
    }

    if(tag->write_data_per_packet < tag->size) {
        multiple_requests = 1;
    }

    cip = (eip_cip_uc_req*)(req->data);

    /* point to the end of the struct */
    data = (req->data) + sizeof(eip_cip_uc_req);

    embed_start = data;

    /*
     * set up the embedded CIP read packet
     * The format is:
     *
     * uint8_t cmd
     * LLA formatted name
     * data type to write
     * uint16_t # of elements to write
     * data to write
     */

    /*
     * set up the CIP Read request type.
     * Different if more than one request.
     *
     * This handles a bug where attempting fragmented requests
     * does not appear to work with a single boolean.
     */
    *data = (multiple_requests) ? AB_EIP_CMD_CIP_WRITE_FRAG : AB_EIP_CMD_CIP_WRITE;
    data++;

    /* copy the tag name into the request */
    mem_copy(data, tag->encoded_name, tag->encoded_name_size);
    data += tag->encoded_name_size;

    /* copy encoded type info */
    if (tag->encoded_type_info_size) {
        mem_copy(data, tag->encoded_type_info, tag->encoded_type_info_size);
        data += tag->encoded_type_info_size;
    } else {
        pdebug(DEBUG_WARN,"Data type unsupported!");
        return PLCTAG_ERR_UNSUPPORTED;
    }

    /* copy the item count, little endian */
    *((uint16_le*)data) = h2le16((uint16_t)(tag->elem_count));
    data += sizeof(uint16_le);

    if (multiple_requests) {
        /* put in the byte offset */
        *((uint32_le*)data) = h2le32((uint32_t)byte_offset);
        data += sizeof(uint32_le);
    }

    /* how much data to write? */
    write_size = tag->size - tag->byte_offset;

    if(write_size > tag->write_data_per_packet) {
        write_size = tag->write_data_per_packet;
    }

    /* now copy the data to write */
    mem_copy(data, tag->data + tag->byte_offset, write_size);
    data += write_size;
    tag->byte_offset += write_size;

    /* need to pad data to multiple of 16-bits */
    if (write_size & 0x01) {
        *data = 0;
        data++;
    }

    /* now we go back and fill in the fields of the static part */
    /* mark the end of the embedded packet */
    embed_end = data;

    /*
     * after the embedded packet, we need to tell the message router
     * how to get to the target device.
     */

    /* Now copy in the routing information for the embedded message */
    *data = (tag->conn_path_size) / 2; /* in 16-bit words */
    data++;
    *data = 0;
    data++;
    mem_copy(data, tag->conn_path, tag->conn_path_size);
    data += tag->conn_path_size;

    /* now fill in the rest of the structure. */

    /* encap fields */
    cip->encap_command = h2le16(AB_EIP_READ_RR_DATA); /* ALWAYS 0x006F Unconnected Send*/

    /* router timeout */
    cip->router_timeout = h2le16(1); /* one second timeout, enough? */

    /* Common Packet Format fields for unconnected send. */
    cip->cpf_item_count = h2le16(2);                  /* ALWAYS 2 */
    cip->cpf_nai_item_type = h2le16(AB_EIP_ITEM_NAI); /* ALWAYS 0 */
    cip->cpf_nai_item_length = h2le16(0);             /* ALWAYS 0 */
    cip->cpf_udi_item_type = h2le16(AB_EIP_ITEM_UDI); /* ALWAYS 0x00B2 - Unconnected Data Item */
    cip->cpf_udi_item_length = h2le16((uint16_t)(data - (uint8_t*)(&(cip->cm_service_code)))); /* REQ: fill in with length of remaining data. */

    /* CM Service Request - Connection Manager */
    cip->cm_service_code = AB_EIP_CMD_UNCONNECTED_SEND; /* 0x52 Unconnected Send */
    cip->cm_req_path_size = 2;                          /* 2, size in 16-bit words of path, next field */
    cip->cm_req_path[0] = 0x20;                         /* class */
    cip->cm_req_path[1] = 0x06;                         /* Connection Manager */
    cip->cm_req_path[2] = 0x24;                         /* instance */
    cip->cm_req_path[3] = 0x01;                         /* instance 1 */

    /* Unconnected send needs timeout information */
    cip->secs_per_tick = AB_EIP_SECS_PER_TICK; /* seconds per tick */
    cip->timeout_ticks = AB_EIP_TIMEOUT_TICKS; /* timeout = srd_secs_per_tick * src_timeout_ticks */

    /* size of embedded packet */
    cip->uc_cmd_length = h2le16((uint16_t)(embed_end - embed_start));

    /* set the size of the request */
    req->request_size = (int)(data - (req->data));

    /* mark it as ready to send */
    req->send_request = 1;

    if(tag->allow_packing) {
        request_allow_packing(req);
    }

    /* add the request to the session's list. */
    rc = session_add_request(tag->session, req);

    if (rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR, "Unable to add request to session! rc=%d", rc);
//        request_release(req);
//        tag->reqs[slot] = rc_dec(req);
        tag->req = rc_dec(req);
        return rc;
    }

    /* save the request for later */
//    tag->reqs[slot] = req;
    tag->req = req;

    pdebug(DEBUG_INFO, "Done");

    return PLCTAG_STATUS_OK;
}


//int build_write_request_unconnected(ab_tag_p tag, int byte_offset)
//{
//    int rc = PLCTAG_STATUS_OK;
//    eip_cip_uc_req* cip;
//    uint8_t* data;
//    uint8_t* embed_start, *embed_end;
//    ab_request_p req = NULL;
//
//    pdebug(DEBUG_INFO, "Starting.");
//
//    /* get a request buffer */
//    rc = request_create(&req, MAX_CIP_MSG_SIZE, tag);
//
//    if (rc != PLCTAG_STATUS_OK) {
//        pdebug(DEBUG_ERROR, "Unable to get new request.  rc=%d", rc);
//        return rc;
//    }
//
////    req->num_retries_left = tag->num_retries;
////    req->retry_interval = tag->default_retry_interval;
//
//    /* point the request struct at the buffer */
//    cip = (eip_cip_uc_req*)(req->data);
//
//    /* point to the end of the struct */
//    data = (req->data) + sizeof(eip_cip_uc_req);
//
//    /*
//     * set up the embedded CIP read packet
//     * The format is:
//     *
//     * uint8_t cmd
//     * LLA formatted name
//     * data type to write
//     * uint16_t # of elements to write
//     * data to write
//     */
//
//    embed_start = data;
//
//    /*
//     * set up the CIP Read request type.
//     * Different if more than one request.
//     *
//     * This handles a bug where attempting fragmented requests
//     * does not appear to work with a single boolean.
//     */
//    *data = (tag->num_write_requests > 1) ? AB_EIP_CMD_CIP_WRITE_FRAG : AB_EIP_CMD_CIP_WRITE;
//    data++;
//
//    /* copy the tag name into the request */
//    mem_copy(data, tag->encoded_name, tag->encoded_name_size);
//    data += tag->encoded_name_size;
//
//    /* copy encoded type info */
//    if (tag->encoded_type_info_size) {
//        mem_copy(data, tag->encoded_type_info, tag->encoded_type_info_size);
//        data += tag->encoded_type_info_size;
//    } else {
//        pdebug(DEBUG_WARN,"Data type unsupported!");
//        return PLCTAG_ERR_UNSUPPORTED;
//    }
//
//    /* copy the item count, little endian */
//    *((uint16_le*)data) = h2le16(tag->elem_count);
//    data += sizeof(uint16_le);
//
//    if (tag->num_write_requests > 1) {
//        /* put in the byte offset */
//        *((uint32_le*)data) = h2le32(byte_offset);
//        data += sizeof(uint32_le);
//    }
//
//    /* now copy the data to write */
//    mem_copy(data, tag->data + byte_offset, tag->write_req_sizes[slot]);
//    data += tag->write_req_sizes[slot];
//
//    /* need to pad data to multiple of 16-bits */
//    if (tag->write_req_sizes[slot] & 0x01) {
//        *data = 0;
//        data++;
//    }
//
//    /* mark the end of the embedded packet */
//    embed_end = data;
//
//    /*
//     * after the embedded packet, we need to tell the message router
//     * how to get to the target device.
//     */
//
//    /* Now copy in the routing information for the embedded message */
//    *data = (tag->conn_path_size) / 2; /* in 16-bit words */
//    data++;
//    *data = 0;
//    data++;
//    mem_copy(data, tag->conn_path, tag->conn_path_size);
//    data += tag->conn_path_size;
//
//    /* now fill in the rest of the structure. */
//
//    /* encap fields */
//    cip->encap_command = h2le16(AB_EIP_READ_RR_DATA); /* ALWAYS 0x006F Unconnected Send*/
//
//    /* router timeout */
//    cip->router_timeout = h2le16(1); /* one second timeout, enough? */
//
//    /* Common Packet Format fields for unconnected send. */
//    cip->cpf_item_count = h2le16(2);                  /* ALWAYS 2 */
//    cip->cpf_nai_item_type = h2le16(AB_EIP_ITEM_NAI); /* ALWAYS 0 */
//    cip->cpf_nai_item_length = h2le16(0);             /* ALWAYS 0 */
//    cip->cpf_udi_item_type = h2le16(AB_EIP_ITEM_UDI); /* ALWAYS 0x00B2 - Unconnected Data Item */
//    cip->cpf_udi_item_length = h2le16(data - (uint8_t*)(&(cip->cm_service_code))); /* REQ: fill in with length of remaining data. */
//
//    /* CM Service Request - Connection Manager */
//    cip->cm_service_code = AB_EIP_CMD_UNCONNECTED_SEND; /* 0x52 Unconnected Send */
//    cip->cm_req_path_size = 2;                          /* 2, size in 16-bit words of path, next field */
//    cip->cm_req_path[0] = 0x20;                         /* class */
//    cip->cm_req_path[1] = 0x06;                         /* Connection Manager */
//    cip->cm_req_path[2] = 0x24;                         /* instance */
//    cip->cm_req_path[3] = 0x01;                         /* instance 1 */
//
//    /* Unconnected send needs timeout information */
//    cip->secs_per_tick = AB_EIP_SECS_PER_TICK; /* seconds per tick */
//    cip->timeout_ticks = AB_EIP_TIMEOUT_TICKS; /* timeout = srd_secs_per_tick * src_timeout_ticks */
//
//    /* size of embedded packet */
//    cip->uc_cmd_length = h2le16(embed_end - embed_start);
//
//    /* set the size of the request */
//    req->request_size = data - (req->data);
//
//    /* mark it as ready to send */
//    req->send_request = 1;
//
//    if(tag->allow_packing) {
//        request_allow_packing(req);
//    }
//
//    /* add the request to the session's list. */
//    rc = session_add_request(tag->session, req);
//
//    if (rc != PLCTAG_STATUS_OK) {
//        pdebug(DEBUG_ERROR, "Unable to add request to session! rc=%d", rc);
////        request_release(req);
////        tag->reqs[slot] = rc_dec(req);
//        tag->req = rc_dec(req);
//        return rc;
//    }
//
//    /* save the request for later */
////    tag->reqs[slot] = req;
//    tag->req = req;
//
//    pdebug(DEBUG_INFO, "Done");
//
//    return PLCTAG_STATUS_OK;
//}
//
//




/*
 * check_read_status_connected
 *
 * This routine checks for any outstanding requests and copies in data
 * that has arrived.  At the end of the request, it will clean up the request
 * buffers.  This is not thread-safe!  It should be called with the tag mutex
 * locked!
 */



static int check_read_status_connected(ab_tag_p tag)
{
    int rc = PLCTAG_STATUS_OK;
    eip_cip_co_resp* cip_resp;
    uint8_t* data;
    uint8_t* data_end;

    pdebug(DEBUG_SPEW, "Starting.");

    if(!tag) {
        pdebug(DEBUG_ERROR,"Null tag pointer passed!");
        return PLCTAG_ERR_NULL_PTR;
    }

    if (!tag->req) {
        tag->read_in_progress = 0;
        pdebug(DEBUG_WARN,"Read in progress, but no request in flight!");
        return PLCTAG_ERR_NULL_PTR;
    }

    if(!tag->req->resp_received) {
        return PLCTAG_STATUS_PENDING;
    }

    /* point to the data */
    cip_resp = (eip_cip_co_resp*)(tag->req->data);

    /* point to the start of the data */
    data = (tag->req->data) + sizeof(eip_cip_co_resp);

    /* point the end of the data */
    data_end = (tag->req->data + le2h16(cip_resp->encap_length) + sizeof(eip_encap_t));

    /* check the status */
    do {
        if (le2h16(cip_resp->encap_command) != AB_EIP_CONNECTED_SEND) {
            pdebug(DEBUG_WARN, "Unexpected EIP packet type received: %d!", cip_resp->encap_command);
            rc = PLCTAG_ERR_BAD_DATA;
            break;
        }

        if (le2h32(cip_resp->encap_status) != AB_EIP_OK) {
            pdebug(DEBUG_WARN, "EIP command failed, response code: %d", le2h32(cip_resp->encap_status));
            rc = PLCTAG_ERR_REMOTE_ERR;
            break;
        }

        /*
         * FIXME
         *
         * It probably should not be necessary to check for both as setting the type to anything other
         * than fragmented is error-prone.
         */

        if (cip_resp->reply_service != (AB_EIP_CMD_CIP_READ_FRAG | AB_EIP_CMD_CIP_OK)
            && cip_resp->reply_service != (AB_EIP_CMD_CIP_READ | AB_EIP_CMD_CIP_OK) ) {
            pdebug(DEBUG_WARN, "CIP response reply service unexpected: %d", cip_resp->reply_service);
            rc = PLCTAG_ERR_BAD_DATA;
            break;
        }

        if (cip_resp->status != AB_CIP_STATUS_OK && cip_resp->status != AB_CIP_STATUS_FRAG) {
            pdebug(DEBUG_WARN, "CIP read failed with status: 0x%x %s", cip_resp->status, decode_cip_error_short((uint8_t *)&cip_resp->status));
            pdebug(DEBUG_INFO, decode_cip_error_long((uint8_t *)&cip_resp->status));

            rc = decode_cip_error_code((uint8_t *)&cip_resp->status);

            break;
        }

        /* the first byte of the response is a type byte. */
        pdebug(DEBUG_DETAIL, "type byte = %d (%x)", (int)*data, (int)*data);

        /* copy the type data. */

        /* check for a simple/base type */

        if ((*data) >= AB_CIP_DATA_BIT && (*data) <= AB_CIP_DATA_STRINGI) {
            /* copy the type info for later. */
            if (tag->encoded_type_info_size == 0) {
                tag->encoded_type_info_size = 2;
                mem_copy(tag->encoded_type_info, data, tag->encoded_type_info_size);
            }

            /* skip the type byte and zero length byte */
            data += 2;
        } else if ((*data) == AB_CIP_DATA_ABREV_STRUCT || (*data) == AB_CIP_DATA_ABREV_ARRAY ||
                   (*data) == AB_CIP_DATA_FULL_STRUCT || (*data) == AB_CIP_DATA_FULL_ARRAY) {
            /* this is an aggregate type of some sort, the type info is variable length */
            int type_length =
                *(data + 1) + 2;  /*
                                   * MAGIC
                                   * add 2 to get the total length including
                                   * the type byte and the length byte.
                                   */

            /* check for extra long types */
            if (type_length > MAX_TAG_TYPE_INFO) {
                pdebug(DEBUG_WARN, "Read data type info is too long (%d)!", type_length);
                rc = PLCTAG_ERR_TOO_LARGE;
                break;
            }

            /* copy the type info for later. */
            if (tag->encoded_type_info_size == 0) {
                tag->encoded_type_info_size = type_length;
                mem_copy(tag->encoded_type_info, data, tag->encoded_type_info_size);
            }

            data += type_length;
        } else {
            pdebug(DEBUG_WARN, "Unsupported data type returned, type byte=%d", *data);
            rc = PLCTAG_ERR_UNSUPPORTED;
            break;
        }

        /* check data size. */
        if ((tag->byte_offset + (data_end - data)) > tag->size) {
            pdebug(DEBUG_WARN,
                   "Read data is too long (%d bytes) to fit in tag data buffer (%d bytes)!",
                   tag->byte_offset + (int)(data_end - data),
                   tag->size);
            pdebug(DEBUG_WARN,"byte_offset=%d, data size=%d", tag->byte_offset, (int)(data_end - data));
            rc = PLCTAG_ERR_TOO_LARGE;
            break;
        }

        pdebug(DEBUG_INFO, "Got %d bytes of data", (data_end - data));

        /*
         * copy the data, but only if this is not
         * a pre-read for a subsequent write!  We do not
         * want to overwrite the data the upstream has
         * put into the tag's data buffer.
         */
        if (!tag->pre_write_read) {
            mem_copy(tag->data + tag->byte_offset, data, (data_end - data));
        }

        /* bump the byte offset */
        tag->byte_offset += (data_end - data);

        /* set the return code */
        rc = PLCTAG_STATUS_OK;
    } while(0);

    /* clean up the request */
    request_abort(tag->req);
    tag->req = rc_dec(tag->req);

    /* are we actually done? */
    if (rc == PLCTAG_STATUS_OK) {
        /* skip if we are doing a pre-write read. */
        if (!tag->pre_write_read && tag->byte_offset < tag->size) {
            /* call read start again to get the next piece */
            pdebug(DEBUG_DETAIL, "calling tag_read_start() to get the next chunk.");
            rc = tag_read_start(tag);
        } else {
            /* done! */
            tag->first_read = 0;
            tag->byte_offset = 0;

            /* if this is a pre-read for a write, then pass off to the write routine */
            if (tag->pre_write_read) {
                pdebug(DEBUG_DETAIL, "Restarting write call now.");

                tag->pre_write_read = 0;
                rc = tag_write_start(tag);
            }

            /* do this after the write starts. This helps prevent races with the client. */
            tag->read_in_progress = 0;
        }
    }

    /* this is not an else clause because the above if could result in bad rc. */
    if(rc != PLCTAG_STATUS_OK && rc != PLCTAG_STATUS_PENDING) {
        /* error ! */
        pdebug(DEBUG_WARN, "Error received!");

        /* clean up everything. */
        ab_tag_abort(tag);
    }

    pdebug(DEBUG_SPEW, "Done.");

    return rc;
}



//
//static int check_read_status_connected(ab_tag_p tag)
//{
//    int rc = PLCTAG_STATUS_OK;
//    eip_cip_co_resp* cip_resp;
//    uint8_t* data;
//    uint8_t* data_end;
//    int i;
////    ab_request_p req;
////    int byte_offset = 0;
//
//    pdebug(DEBUG_DETAIL, "Starting.");
//
//    /* is there an outstanding request? */
////    if (!tag->reqs) {
////        tag->read_in_progress = 0;
////        pdebug(DEBUG_WARN,"Read in progress, but no requests in flight!");
////        return PLCTAG_ERR_NULL_PTR;
////    }
////
////    for (i = 0; i < tag->num_read_requests; i++) {
////        if (tag->reqs[i] && !tag->reqs[i]->resp_received) {
////            return PLCTAG_STATUS_PENDING;
////        }
////    }
//
//    if(!tag->req) {
//        tag->read_in_progress = 0;
//        pdebug(DEBUG_WARN,"Read in progress, but no request in flight!");
//        return PLCTAG_ERR_NULL_PTR;
//    }
//
//    if(!tag->req->resp_received) {
//        return PLCTAG_STATUS_PENDING;
//    }
//
//    /*
//     * process each request.  If there is more than one request, then
//     * we need to make sure that we copy the data into the right part
//     * of the tag's data buffer.
//     */
////    for (i = 0; i < tag->num_read_requests; i++) {
////        req = tag->reqs[i];
////
////        if (!req) {
////            rc = PLCTAG_ERR_NULL_PTR;
////            break;
////        }
////
////        /* skip if already processed */
////        if (req->processed) {
////            byte_offset += tag->read_req_sizes[i];
////            continue;
////        }
//
////    req->processed = 1;
//
////        pdebug(DEBUG_DETAIL, "processing request %d", i);
//
//    /* point to the data */
//    cip_resp = (eip_cip_co_resp*)(tag->req->data);
//
//    /* point to the start of the data */
//    data = (tag->req->data) + sizeof(eip_cip_co_resp);
//
//    /* point the end of the data */
//    data_end = (tag->req->data + le2h16(cip_resp->encap_length) + sizeof(eip_encap_t));
//
//    /* check the status */
//    if (le2h16(cip_resp->encap_command) != AB_EIP_CONNECTED_SEND) {
//        pdebug(DEBUG_WARN, "Unexpected EIP packet type received: %d!", cip_resp->encap_command);
//        rc = PLCTAG_ERR_BAD_DATA;
//        break;
//    }
//
//    if (le2h32(cip_resp->encap_status) != AB_EIP_OK) {
//        pdebug(DEBUG_WARN, "EIP command failed, response code: %d", le2h32(cip_resp->encap_status));
//        rc = PLCTAG_ERR_REMOTE_ERR;
//        break;
//    }
//
//    /*
//     * FIXME
//     *
//     * It probably should not be necessary to check for both as setting the type to anything other
//     * than fragmented is error-prone.
//     */
//
//    if (cip_resp->reply_service != (AB_EIP_CMD_CIP_READ_FRAG | AB_EIP_CMD_CIP_OK)
//        && cip_resp->reply_service != (AB_EIP_CMD_CIP_READ | AB_EIP_CMD_CIP_OK) ) {
//        pdebug(DEBUG_WARN, "CIP response reply service unexpected: %d", cip_resp->reply_service);
//        rc = PLCTAG_ERR_BAD_DATA;
//        break;
//    }
//
//    if (cip_resp->status != AB_CIP_STATUS_OK && cip_resp->status != AB_CIP_STATUS_FRAG) {
//        pdebug(DEBUG_WARN, "CIP read failed with status: 0x%x %s", cip_resp->status, decode_cip_error_short((uint8_t *)&cip_resp->status));
//        pdebug(DEBUG_INFO, decode_cip_error_long((uint8_t *)&cip_resp->status));
//
//        rc = decode_cip_error_code((uint8_t *)&cip_resp->status);
//
//        break;
//    }
//
//    /* the first byte of the response is a type byte. */
//    pdebug(DEBUG_DETAIL, "type byte = %d (%x)", (int)*data, (int)*data);
//
//    /*
//     * AB has a relatively complicated scheme for data typing.  The type is
//     * required when writing.  Most of the types are basic types and occupy
//     * a known amount of space.  Aggregate types like structs and arrays
//     * occupy a variable amount of space.  In addition, structs and arrays
//     * can be in two forms: full and abbreviated.  Full form for structs includes
//     * all data types (in full) for fields of the struct.  Abbreviated form for
//     * structs includes a two byte CRC calculated across the full form.  For arrays,
//     * full form includes index limits and base data type.  Abbreviated arrays
//     * drop the limits and encode any structs as abbreviate structs.  At least
//     * we think this is what is happening.
//     *
//     * Luckily, we do not actually care what these bytes mean, we just need
//     * to copy them and skip past them for the actual data.
//     */
//
//    /* check for a simple/base type */
//    if ((*data) >= AB_CIP_DATA_BIT && (*data) <= AB_CIP_DATA_STRINGI) {
//        /* copy the type info for later. */
//        if (tag->encoded_type_info_size == 0) {
//            tag->encoded_type_info_size = 2;
//            mem_copy(tag->encoded_type_info, data, tag->encoded_type_info_size);
//        }
//
//        /* skip the type byte and zero length byte */
//        data += 2;
//    } else if ((*data) == AB_CIP_DATA_ABREV_STRUCT || (*data) == AB_CIP_DATA_ABREV_ARRAY ||
//               (*data) == AB_CIP_DATA_FULL_STRUCT || (*data) == AB_CIP_DATA_FULL_ARRAY) {
//        /* this is an aggregate type of some sort, the type info is variable length */
//        int type_length =
//            *(data + 1) + 2; /*
//                                    * MAGIC
//                                    * add 2 to get the total length including
//                                    * the type byte and the length byte.
//                                    */
//
//        /* check for extra long types */
//        if (type_length > MAX_TAG_TYPE_INFO) {
//            pdebug(DEBUG_WARN, "Read data type info is too long (%d)!", type_length);
//            rc = PLCTAG_ERR_TOO_LARGE;
//            break;
//        }
//
//        /* copy the type info for later. */
//        if (tag->encoded_type_info_size == 0) {
//            tag->encoded_type_info_size = type_length;
//            mem_copy(tag->encoded_type_info, data, tag->encoded_type_info_size);
//        }
//
//        data += type_length;
//    } else {
//        pdebug(DEBUG_WARN, "Unsupported data type returned, type byte=%d", *data);
//        rc = PLCTAG_ERR_UNSUPPORTED;
//        break;
//    }
//
//    /* copy data into the tag. */
//    if ((tag->byte_offset + (data_end - data)) > tag->size) {
//        pdebug(DEBUG_WARN,
//               "Read data is too long (%d bytes) to fit in tag data buffer (%d bytes)!",
//               tag->byte_offset + (int)(data_end - data),
//               tag->size);
//        rc = PLCTAG_ERR_TOO_LARGE;
//        break;
//    }
//
//    pdebug(DEBUG_DETAIL, "Got %d bytes of data", (data_end - data));
//
//    /*
//     * copy the data, but only if this is not
//     * a pre-read for a subsequent write!  We do not
//     * want to overwrite the data the upstream has
//     * put into the tag's data buffer.
//     */
//    if (!tag->pre_write_read) {
//        mem_copy(tag->data + tag->byte_offset, data, (data_end - data));
//    }
//
////        /* save the size of the response for next time */
////        tag->read_req_sizes[i] = (data_end - data);
//
//    /*
//     * did we get any data back? a zero-length response is
//     * an error here.
//     */
//
////        if ((data_end - data) == 0) {
////            rc = PLCTAG_ERR_NO_DATA;
////            break;
////        } else {
//    /* bump the byte offset */
//    tag->byte_offset += (data_end - data);
//
//    /* set the return code */
//    rc = PLCTAG_STATUS_OK;
////        }
////    } /* end of for(i = 0; i < tag->num_requests; i++) */
//
//    /* are we actually done? */
//    if (rc == PLCTAG_STATUS_OK) {
//        /* skip if we are doing a pre-write read. */
//        if (!tag->pre_write_read && tag->byte_offset < tag->size) {
//            /* no, not yet */
////            if (tag->first_read) {
//            /* call read start again to get the next piece */
//            pdebug(DEBUG_DETAIL, "calling tag_read_start() to get the next chunk.");
//            rc = tag_read_start(tag);
////            } else {
////                pdebug(DEBUG_WARN, "Insufficient data read for tag!");
////                ab_tag_abort(tag);
////                rc = PLCTAG_ERR_READ;
////            }
//        } else {
//            /* done! */
//            tag->first_read = 0;
//            tag->byte_offset = 0;
//
//            /* have the IO thread take care of the request buffers */
//            request_abort(tag->req);
//            tag->req = rc_dec(tag->req);
//
//            /* if this is a pre-read for a write, then pass off to the write routine */
//            if (tag->pre_write_read) {
//                pdebug(DEBUG_DETAIL, "Restarting write call now.");
//
//                tag->pre_write_read = 0;
//                rc = tag_write_start(tag);
//            }
//
//            /* do this after the write starts. This helps prevent races with the client. */
//            tag->read_in_progress = 0;
//        }
//    } else {
//        /* error ! */
//        pdebug(DEBUG_WARN, "Error received!");
//
//        /* have the IO thread take care of the request buffers */
//        request_abort(tag->req);
//        tag->req = rc_dec(tag->req);
//    }
//
//    pdebug(DEBUG_INFO, "Done.");
//
//    return rc;
//}
//


static int check_read_status_unconnected(ab_tag_p tag)
{
    int rc = PLCTAG_STATUS_OK;
    eip_cip_uc_resp* cip_resp;
    uint8_t* data;
    uint8_t* data_end;

    pdebug(DEBUG_SPEW, "Starting.");

    if(!tag) {
        pdebug(DEBUG_ERROR,"Null tag pointer passed!");
        return PLCTAG_ERR_NULL_PTR;
    }

    if (!tag->req) {
        tag->read_in_progress = 0;
        pdebug(DEBUG_WARN,"Read in progress, but no request in flight!");
        return PLCTAG_ERR_NULL_PTR;
    }

    if(!tag->req->resp_received) {
        return PLCTAG_STATUS_PENDING;
    }

    /* point to the data */
    cip_resp = (eip_cip_uc_resp*)(tag->req->data);

    /* point to the start of the data */
    data = (tag->req->data) + sizeof(eip_cip_uc_resp);

    /* point the end of the data */
    data_end = (tag->req->data + le2h16(cip_resp->encap_length) + sizeof(eip_encap_t));

    /* check the status */
    do {
        if (le2h16(cip_resp->encap_command) != AB_EIP_READ_RR_DATA) {
            pdebug(DEBUG_WARN, "Unexpected EIP packet type received: %d!", cip_resp->encap_command);
            rc = PLCTAG_ERR_BAD_DATA;
            break;
        }

        if (le2h32(cip_resp->encap_status) != AB_EIP_OK) {
            pdebug(DEBUG_WARN, "EIP command failed, response code: %d", le2h32(cip_resp->encap_status));
            rc = PLCTAG_ERR_REMOTE_ERR;
            break;
        }

        /*
         * FIXME
         *
         * It probably should not be necessary to check for both as setting the type to anything other
         * than fragmented is error-prone.
         */

        if (cip_resp->reply_service != (AB_EIP_CMD_CIP_READ_FRAG | AB_EIP_CMD_CIP_OK)
            && cip_resp->reply_service != (AB_EIP_CMD_CIP_READ | AB_EIP_CMD_CIP_OK) ) {
            pdebug(DEBUG_WARN, "CIP response reply service unexpected: %d", cip_resp->reply_service);
            rc = PLCTAG_ERR_BAD_DATA;
            break;
        }

        if (cip_resp->status != AB_CIP_STATUS_OK && cip_resp->status != AB_CIP_STATUS_FRAG) {
            pdebug(DEBUG_WARN, "CIP read failed with status: 0x%x %s", cip_resp->status, decode_cip_error_short((uint8_t *)&cip_resp->status));
            pdebug(DEBUG_INFO, decode_cip_error_long((uint8_t *)&cip_resp->status));

            rc = decode_cip_error_code((uint8_t *)&cip_resp->status);

            break;
        }

        /* the first byte of the response is a type byte. */
        pdebug(DEBUG_DETAIL, "type byte = %d (%x)", (int)*data, (int)*data);

        /* copy the type data. */

        /* check for a simple/base type */

        if ((*data) >= AB_CIP_DATA_BIT && (*data) <= AB_CIP_DATA_STRINGI) {
            /* copy the type info for later. */
            if (tag->encoded_type_info_size == 0) {
                tag->encoded_type_info_size = 2;
                mem_copy(tag->encoded_type_info, data, tag->encoded_type_info_size);
            }

            /* skip the type byte and zero length byte */
            data += 2;
        } else if ((*data) == AB_CIP_DATA_ABREV_STRUCT || (*data) == AB_CIP_DATA_ABREV_ARRAY ||
                   (*data) == AB_CIP_DATA_FULL_STRUCT || (*data) == AB_CIP_DATA_FULL_ARRAY) {
            /* this is an aggregate type of some sort, the type info is variable length */
            int type_length =
                *(data + 1) + 2;  /*
                                   * MAGIC
                                   * add 2 to get the total length including
                                   * the type byte and the length byte.
                                   */

            /* check for extra long types */
            if (type_length > MAX_TAG_TYPE_INFO) {
                pdebug(DEBUG_WARN, "Read data type info is too long (%d)!", type_length);
                rc = PLCTAG_ERR_TOO_LARGE;
                break;
            }

            /* copy the type info for later. */
            if (tag->encoded_type_info_size == 0) {
                tag->encoded_type_info_size = type_length;
                mem_copy(tag->encoded_type_info, data, tag->encoded_type_info_size);
            }

            data += type_length;
        } else {
            pdebug(DEBUG_WARN, "Unsupported data type returned, type byte=%d", *data);
            rc = PLCTAG_ERR_UNSUPPORTED;
            break;
        }

        /* copy data into the tag. */
        if ((tag->byte_offset + (data_end - data)) > tag->size) {
            pdebug(DEBUG_WARN,
                   "Read data is too long (%d bytes) to fit in tag data buffer (%d bytes)!",
                   tag->byte_offset + (int)(data_end - data),
                   tag->size);
            pdebug(DEBUG_WARN,"byte_offset=%d, data size=%d", tag->byte_offset, (int)(data_end - data));
            rc = PLCTAG_ERR_TOO_LARGE;
            break;
        }

        pdebug(DEBUG_INFO, "Got %d bytes of data", (data_end - data));

        /*
         * copy the data, but only if this is not
         * a pre-read for a subsequent write!  We do not
         * want to overwrite the data the upstream has
         * put into the tag's data buffer.
         */
        if (!tag->pre_write_read) {
            mem_copy(tag->data + tag->byte_offset, data, (data_end - data));
        }

        /* bump the byte offset */
        tag->byte_offset += (data_end - data);

        /* set the return code */
        rc = PLCTAG_STATUS_OK;
    } while(0);


    /* clean up the request */
    request_abort(tag->req);
    tag->req = rc_dec(tag->req);

    /* are we actually done? */
    if (rc == PLCTAG_STATUS_OK) {
        /* skip if we are doing a pre-write read. */
        if (!tag->pre_write_read && tag->byte_offset < tag->size) {
            /* call read start again to get the next piece */
            pdebug(DEBUG_DETAIL, "calling tag_read_start() to get the next chunk.");
            rc = tag_read_start(tag);
        } else {
            /* done! */
            tag->first_read = 0;
            tag->byte_offset = 0;

            /* if this is a pre-read for a write, then pass off to the write routine */
            if (tag->pre_write_read) {
                pdebug(DEBUG_DETAIL, "Restarting write call now.");

                tag->pre_write_read = 0;
                rc = tag_write_start(tag);
            }

            /* do this after the write starts. This helps prevent races with the client. */
            tag->read_in_progress = 0;
        }
    }

    /* this is not an else clause because the above if could result in bad rc. */
    if(rc != PLCTAG_STATUS_OK && rc != PLCTAG_STATUS_PENDING) {
        /* error ! */
        pdebug(DEBUG_WARN, "Error received!");

        /* clean up everything. */
        ab_tag_abort(tag);
    }

    pdebug(DEBUG_INFO, "Done.");

    return rc;
}



/*
 * check_write_status_connected
 *
 * This routine must be called with the tag mutex locked.  It checks the current
 * status of a write operation.  If the write is done, it triggers the clean up.
 */


static int check_write_status_connected(ab_tag_p tag)
{
    eip_cip_co_resp* cip_resp;
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_SPEW, "Starting.");

    if(!tag) {
        pdebug(DEBUG_ERROR,"Null tag pointer passed!");
        return PLCTAG_ERR_NULL_PTR;
    }

    /* is there an outstanding request? */
    if(!tag->req) {
        tag->write_in_progress = 0;
        pdebug(DEBUG_INFO, "Write in progress but no outstanding write request!");
        return PLCTAG_ERR_NULL_PTR;
    }

    if (!tag->req->resp_received) {
        return PLCTAG_STATUS_PENDING;
    }

    /* point to the data */
    cip_resp = (eip_cip_co_resp*)(tag->req->data);

    do {
        if (le2h16(cip_resp->encap_command) != AB_EIP_CONNECTED_SEND) {
            pdebug(DEBUG_WARN, "Unexpected EIP packet type received: %d!", cip_resp->encap_command);
            rc = PLCTAG_ERR_BAD_DATA;
            break;
        }

        if (le2h32(cip_resp->encap_status) != AB_EIP_OK) {
            pdebug(DEBUG_WARN, "EIP command failed, response code: %d", le2h32(cip_resp->encap_status));
            rc = PLCTAG_ERR_REMOTE_ERR;
            break;
        }

        if (cip_resp->reply_service != (AB_EIP_CMD_CIP_WRITE_FRAG | AB_EIP_CMD_CIP_OK)
            && cip_resp->reply_service != (AB_EIP_CMD_CIP_WRITE | AB_EIP_CMD_CIP_OK)) {
            pdebug(DEBUG_WARN, "CIP response reply service unexpected: %d", cip_resp->reply_service);
            rc = PLCTAG_ERR_BAD_DATA;
            break;
        }

        if (cip_resp->status != AB_CIP_STATUS_OK && cip_resp->status != AB_CIP_STATUS_FRAG) {
            pdebug(DEBUG_WARN, "CIP read failed with status: 0x%x %s", cip_resp->status, decode_cip_error_short((uint8_t *)&cip_resp->status));
            pdebug(DEBUG_INFO, decode_cip_error_long((uint8_t *)&cip_resp->status));
            rc = decode_cip_error_code((uint8_t *)&cip_resp->status);
            break;
        }
    } while(0);

    /* clean up the request. */
    request_abort(tag->req);
    tag->req = rc_dec(tag->req);

    if(rc == PLCTAG_STATUS_OK) {
        if(tag->byte_offset < tag->size) {

            pdebug(DEBUG_DETAIL, "Write not complete, triggering next round.");
            rc = tag_write_start(tag);
        } else {
            /* only clear this if we are done. */
            tag->write_in_progress = 0;
            tag->byte_offset = 0;
        }
    } else {
        pdebug(DEBUG_WARN,"Write failed!");

        tag->write_in_progress = 0;
        tag->byte_offset = 0;
    }

            pdebug(DEBUG_DETAIL, "Write not complete, triggering next round.");
            rc = tag_write_start(tag);
        } else {
            /* only clear this if we are done. */
            tag->write_in_progress = 0;
            tag->byte_offset = 0;
        }
    } else {
        pdebug(DEBUG_WARN,"Write failed!");

        tag->write_in_progress = 0;
        tag->byte_offset = 0;
    }

    pdebug(DEBUG_SPEW, "Done.");

    return rc;
}



//static int check_write_status_connected(ab_tag_p tag)
//{
//    eip_cip_co_resp* cip_resp;
//    int rc = PLCTAG_STATUS_OK;
////    int i;
////    ab_request_p req;
//
//    pdebug(DEBUG_DETAIL, "Starting.");
//
//    if(!tag) {
//        pdebug(DEBUG_ERROR,"Null tag pointer passed!");
//        return PLCTAG_ERR_NULL_PTR;
//    }
//
//    /* is there an outstanding request? */
//    if (!tag->reqs) {
//        tag->write_in_progress = 0;
//        pdebug(DEBUG_INFO, "Write in progress but noo outstanding requests!");
//        return PLCTAG_ERR_NULL_PTR;
//    }
//
//    for (i = 0; i < tag->num_write_requests; i++) {
//        if (tag->reqs[i] && !tag->reqs[i]->resp_received) {
//            return PLCTAG_STATUS_PENDING;
//        }
//    }
//
//    if(!tag->req) {
//        tag->write_in_progress = 0;
//        pdebug(DEBUG_INFO, "Write in progress but no outstanding write request!");
//        return PLCTAG_ERR_NULL_PTR;
//    }
//
//    if (!tag->req->resp_received) {
//        return PLCTAG_STATUS_PENDING;
//    }
//
//    /*
//     * process each request.  If there is more than one request, then
//     * we need to make sure that we copy the data into the right part
//     * of the tag's data buffer.
//     */
////    for (i = 0; i < tag->num_write_requests; i++) {
////        int reply_service;
////
////        req = tag->reqs[i];
////
////        if (!req) {
////            rc = PLCTAG_ERR_NULL_PTR;
////            break;
////        }
//
//    do {
//        /* point to the data */
//        cip_resp = (eip_cip_co_resp*)(tag->req->data);
//
//        if (le2h16(cip_resp->encap_command) != AB_EIP_CONNECTED_SEND) {
//            pdebug(DEBUG_WARN, "Unexpected EIP packet type received: %d!", cip_resp->encap_command);
//            rc = PLCTAG_ERR_BAD_DATA;
//            break;
//        }
//
//        if (le2h32(cip_resp->encap_status) != AB_EIP_OK) {
//            pdebug(DEBUG_WARN, "EIP command failed, response code: %d", le2h32(cip_resp->encap_status));
//            rc = PLCTAG_ERR_REMOTE_ERR;
//            break;
//        }
//
//        /* if we have fragmented the request, we need to look for a different return code */
//        reply_service = ((tag->num_write_requests > 1) ? (AB_EIP_CMD_CIP_WRITE_FRAG | AB_EIP_CMD_CIP_OK) :
//                         (AB_EIP_CMD_CIP_WRITE | AB_EIP_CMD_CIP_OK));
//
//        if (cip_resp->reply_service != reply_service) {
//            pdebug(DEBUG_WARN, "CIP response reply service unexpected: %d", cip_resp->reply_service);
//            rc = PLCTAG_ERR_BAD_DATA;
//            break;
//        }
//
//        if (cip_resp->status != AB_CIP_STATUS_OK && cip_resp->status != AB_CIP_STATUS_FRAG) {
//            pdebug(DEBUG_WARN, "CIP read failed with status: 0x%x %s", cip_resp->status, decode_cip_error_short((uint8_t *)&cip_resp->status));
//            pdebug(DEBUG_INFO, decode_cip_error_long((uint8_t *)&cip_resp->status));
//            rc = decode_cip_error_code((uint8_t *)&cip_resp->status);
//            break;
//        }
//    } while(0);
////}
//
//    /* clean up the request. */
//    request_abort(tag->req);
//    tag->req = rc_dec(tag->req);
//
//    if(rc == PLCTAG_STATUS_OK) {
//        if(tag->byte_offset < tag->size) {
//
//            pdebug(DEBUG_DETAIL, "Write not complete, triggering next round.");
//            rc = tag_write_start(tag);
//        } else {
//            /* only clear this if we are done. */
//            tag->write_in_progress = 0;
//        }
//    } else {
//        pdebug(DEBUG_WARN,"Write failed!");
//
//        tag->write_in_progress = 0;
//    }
//
//    pdebug(DEBUG_DETAIL, "Done.");
//
//    return rc;
//}



static int check_write_status_unconnected(ab_tag_p tag)
{
    eip_cip_uc_resp* cip_resp;
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_SPEW, "Starting.");

    if(!tag) {
        pdebug(DEBUG_ERROR,"Null tag pointer passed!");
        return PLCTAG_ERR_NULL_PTR;
    }

    if(!tag) {
        pdebug(DEBUG_ERROR,"Null tag pointer passed!");
        return PLCTAG_ERR_NULL_PTR;
    }

    /* is there an outstanding request? */
    if(!tag->req) {
        tag->write_in_progress = 0;
        pdebug(DEBUG_INFO, "Write in progress but no outstanding write request!");
        return PLCTAG_ERR_NULL_PTR;
    }

    if (!tag->req->resp_received) {
        return PLCTAG_STATUS_PENDING;
    }

    /* point to the data */
    cip_resp = (eip_cip_uc_resp*)(tag->req->data);

    do {
        if (le2h16(cip_resp->encap_command) != AB_EIP_CONNECTED_SEND) {
            pdebug(DEBUG_WARN, "Unexpected EIP packet type received: %d!", cip_resp->encap_command);
            rc = PLCTAG_ERR_BAD_DATA;
            break;
        }

        if (le2h32(cip_resp->encap_status) != AB_EIP_OK) {
            pdebug(DEBUG_WARN, "EIP command failed, response code: %d", le2h32(cip_resp->encap_status));
            rc = PLCTAG_ERR_REMOTE_ERR;
            break;
        }

        if (cip_resp->reply_service != (AB_EIP_CMD_CIP_WRITE_FRAG | AB_EIP_CMD_CIP_OK)
            && cip_resp->reply_service != (AB_EIP_CMD_CIP_WRITE | AB_EIP_CMD_CIP_OK)) {
            pdebug(DEBUG_WARN, "CIP response reply service unexpected: %d", cip_resp->reply_service);
            rc = PLCTAG_ERR_BAD_DATA;
            break;
        }

        if (cip_resp->status != AB_CIP_STATUS_OK && cip_resp->status != AB_CIP_STATUS_FRAG) {
            pdebug(DEBUG_WARN, "CIP read failed with status: 0x%x %s", cip_resp->status, decode_cip_error_short((uint8_t *)&cip_resp->status));
            pdebug(DEBUG_INFO, decode_cip_error_long((uint8_t *)&cip_resp->status));
            rc = decode_cip_error_code((uint8_t *)&cip_resp->status);
            break;
        }
    } while(0);

    /* clean up the request. */
    request_abort(tag->req);
    tag->req = rc_dec(tag->req);

    if(rc == PLCTAG_STATUS_OK) {
        if(tag->byte_offset < tag->size) {

            pdebug(DEBUG_DETAIL, "Write not complete, triggering next round.");
            rc = tag_write_start(tag);
        } else {
            /* only clear this if we are done. */
            tag->write_in_progress = 0;
            tag->byte_offset = 0;
        }
    } else {
        pdebug(DEBUG_WARN,"Write failed!");

        tag->write_in_progress = 0;
        tag->byte_offset = 0;
    }

        tag->write_in_progress = 0;
        tag->byte_offset = 0;
    }

    pdebug(DEBUG_SPEW, "Done.");

    return rc;
}


//
//int check_write_status_unconnected(ab_tag_p tag)
//{
//    eip_cip_uc_resp* cip_resp;
//    int rc = PLCTAG_STATUS_OK;
//    int i;
//    ab_request_p req;
//
//    pdebug(DEBUG_DETAIL, "Starting.");
//
//    /* is there an outstanding request? */
//    if (!tag->reqs) {
//        tag->write_in_progress = 0;
//        pdebug(DEBUG_WARN,"Write in progress, but no requests in flight!");
//        return PLCTAG_ERR_NULL_PTR;
//    }
//
//    for (i = 0; i < tag->num_write_requests; i++) {
//        if (tag->reqs[i] && !tag->reqs[i]->resp_received) {
//            return PLCTAG_STATUS_PENDING;
//        }
//    }
//
//    /*
//     * process each request.  If there is more than one request, then
//     * we need to make sure that we copy the data into the right part
//     * of the tag's data buffer.
//     */
//    for (i = 0; i < tag->num_write_requests; i++) {
//        int reply_service;
//
//        req = tag->reqs[i];
//
//        if (!req) {
//            rc = PLCTAG_ERR_NULL_PTR;
//            break;
//        }
//
//        /* point to the data */
//        cip_resp = (eip_cip_uc_resp*)(req->data);
//
//        if (le2h16(cip_resp->encap_command) != AB_EIP_READ_RR_DATA) {
//            pdebug(DEBUG_WARN, "Unexpected EIP packet type received: %d!", cip_resp->encap_command);
//            rc = PLCTAG_ERR_BAD_DATA;
//            break;
//        }
//
//        if (le2h32(cip_resp->encap_status) != AB_EIP_OK) {
//            pdebug(DEBUG_WARN, "EIP command failed, response code: %d", le2h32(cip_resp->encap_status));
//            rc = PLCTAG_ERR_REMOTE_ERR;
//            break;
//        }
//
//        /* if we have fragmented the request, we need to look for a different return code */
//        reply_service = ((tag->num_write_requests > 1) ? (AB_EIP_CMD_CIP_WRITE_FRAG | AB_EIP_CMD_CIP_OK) :
//                         (AB_EIP_CMD_CIP_WRITE | AB_EIP_CMD_CIP_OK));
//
//        if (cip_resp->reply_service != reply_service) {
//            pdebug(DEBUG_WARN, "CIP response reply service unexpected: %d", cip_resp->reply_service);
//            rc = PLCTAG_ERR_BAD_DATA;
//            break;
//        }
//
//        if (cip_resp->status != AB_CIP_STATUS_OK && cip_resp->status != AB_CIP_STATUS_FRAG) {
//            pdebug(DEBUG_WARN, "CIP read failed with status: 0x%x %s", cip_resp->status, decode_cip_error_short((uint8_t *)&cip_resp->status));
//            pdebug(DEBUG_INFO, decode_cip_error_long((uint8_t *)&cip_resp->status));
//            rc = decode_cip_error_code((uint8_t *)&cip_resp->status);
//            break;
//        }
//    }
//
//    /* this triggers the clean up */
//    ab_tag_abort(tag);
//
//    tag->write_in_progress = 0;
//
//    pdebug(DEBUG_DETAIL, "Done.");
//
//    return rc;
//}
//


int calculate_write_data_per_packet(ab_tag_p tag)
{
    int overhead = 0;
    int data_per_packet = 0;
    int max_payload_size = 0;

    pdebug(DEBUG_DETAIL, "Starting.");

    if (tag->write_data_per_packet > 0) {
        pdebug(DEBUG_DETAIL, "Early termination, write sizes already calculated.");
        return tag->write_data_per_packet;
    }

    /* if we are here, then we have all the type data etc. */
    if(tag->use_connected_msg) {
        pdebug(DEBUG_DETAIL,"Connected tag.");
        max_payload_size = tag->session->max_payload_size;
        overhead =  1                               /* service request, one byte */
                    + tag->encoded_name_size        /* full encoded name */
                    + tag->encoded_type_info_size   /* encoded type size */
                    + 2                             /* element count, 16-bit int */
                    + 4                             /* byte offset, 32-bit int */
                    + 8;                            /* MAGIC fudge factor */
    } else {
        max_payload_size = MAX_CIP_MSG_SIZE;
        overhead =  1                               /* service request, one byte */
                    + tag->encoded_name_size        /* full encoded name */
                    + tag->encoded_type_info_size   /* encoded type size */
                    + tag->conn_path_size + 2       /* encoded device path size plus two bytes for length and padding */
                    + 2                             /* element count, 16-bit int */
                    + 4                             /* byte offset, 32-bit int */
                    + 8;                            /* MAGIC fudge factor */
    }

    data_per_packet = max_payload_size - overhead;

    pdebug(DEBUG_DETAIL,"Write packet maximum size is %d, write overhead is %d, and write data per packet is %d.", max_payload_size, overhead, data_per_packet);

    if (data_per_packet <= 0) {
        pdebug(DEBUG_WARN,
               "Unable to send request.  Packet overhead, %d bytes, is too large for packet, %d bytes!",
               overhead,
               max_payload_size);
        return PLCTAG_ERR_TOO_LARGE;
    }

    /* we want a multiple of 8 bytes */
    data_per_packet &= 0xFFFFF8;

    tag->write_data_per_packet = data_per_packet;

    return data_per_packet;
}

//int calculate_write_sizes(ab_tag_p tag)
//{
//    int overhead = 0;
//    int data_per_packet = 0;
//    int num_reqs = 0;
//    int rc = PLCTAG_STATUS_OK;
//    int i = 0;
//    int byte_offset = 0;
//    int max_payload_size = 0;
//
//    pdebug(DEBUG_DETAIL, "Starting.");
//
//    if (tag->num_write_requests > 0) {
//        pdebug(DEBUG_DETAIL, "Early termination, write sizes already calculated.");
//        return rc;
//    }
//
//    /* if we are here, then we have all the type data etc. */
//    if(tag->connection) {
//        pdebug(DEBUG_DETAIL,"Connected tag.");
//        max_payload_size = tag->connection->max_payload_size;
//        overhead =  1                               /* service request, one byte */
//                    + tag->encoded_name_size        /* full encoded name */
//                    + tag->encoded_type_info_size   /* encoded type size */
//                    + 2                             /* element count, 16-bit int */
//                    + 4                             /* byte offset, 32-bit int */
//                    + 8;                            /* MAGIC fudge factor */
//    } else {
//        max_payload_size = MAX_CIP_MSG_SIZE;
//        overhead =  1                               /* service request, one byte */
//                    + tag->encoded_name_size        /* full encoded name */
//                    + tag->encoded_type_info_size   /* encoded type size */
//                    + tag->conn_path_size + 2       /* encoded device path size plus two bytes for length and padding */
//                    + 2                             /* element count, 16-bit int */
//                    + 4                             /* byte offset, 32-bit int */
//                    + 8;                            /* MAGIC fudge factor */
//    }
//
//    data_per_packet = max_payload_size - overhead;
//
//    pdebug(DEBUG_DETAIL,"Write packet maximum size is %d, write overhead is %d, and write data per packet is %d.", max_payload_size, overhead, data_per_packet);
//
//    if (data_per_packet <= 0) {
//        pdebug(DEBUG_WARN,
//               "Unable to send request.  Packet overhead, %d bytes, is too large for packet, %d bytes!",
//               overhead,
//               max_payload_size);
//        return PLCTAG_ERR_TOO_LARGE;
//    }
//
//    /* we want a multiple of 8 bytes */
//    data_per_packet &= 0xFFFFF8;
//
//    num_reqs = (tag->size + (data_per_packet - 1)) / data_per_packet;
//
//    pdebug(DEBUG_DETAIL, "We need %d requests.", num_reqs);
//
//    byte_offset = 0;
//
//    for (i = 0; i < num_reqs && rc == PLCTAG_STATUS_OK; i++) {
//        /* allocate a new slot */
//        rc = allocate_write_request_slot(tag);
//
//        if (rc == PLCTAG_STATUS_OK) {
//            /* how much data are we going to write in this packet? */
//            if ((tag->size - byte_offset) > data_per_packet) {
//                tag->write_req_sizes[i] = data_per_packet;
//            } else {
//                tag->write_req_sizes[i] = (tag->size - byte_offset);
//            }
//
//            pdebug(DEBUG_DETAIL, "Request %d is of size %d.", i, tag->write_req_sizes[i]);
//
//            /* update the byte offset for the next packet */
//            byte_offset += tag->write_req_sizes[i];
//        }
//    }
//
//    pdebug(DEBUG_DETAIL, "Done.");
//
//    return rc;
//}
