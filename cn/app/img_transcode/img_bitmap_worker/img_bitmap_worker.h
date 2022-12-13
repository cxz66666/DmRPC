#pragma once
#include <hdr/hdr_histogram.h>
#include "atomic_queue/atomic_queue.h"
#include "../img_transcode_commons.h"
#include "bmp.h"
using SPSC_QUEUE = atomic_queue::AtomicQueueB2<erpc::MsgBuffer, std::allocator<erpc::MsgBuffer>, true, false, true>;

DEFINE_double(resize_factor, 0.5, "the resize factor, must be between 0 and 1");

class ClientContext : public BasicContext
{

public:
    ClientContext(size_t cid, size_t sid, size_t rid) : client_id_(cid), server_sender_id_(sid), server_receiver_id_(rid)
    {
        forward_spsc_queue = new SPSC_QUEUE(kAppMaxConcurrency);
        backward_spsc_queue = new SPSC_QUEUE(kAppMaxConcurrency);
    }
    ~ClientContext()
    {
        delete forward_spsc_queue;
        delete backward_spsc_queue;
    }
    erpc::MsgBuffer req_backward_msgbuf[kAppMaxConcurrency];

    erpc::MsgBuffer resp_backward_msgbuf[kAppMaxConcurrency];

    size_t client_id_;
    size_t server_sender_id_;
    size_t server_receiver_id_;

    SPSC_QUEUE *forward_spsc_queue;
    SPSC_QUEUE *backward_spsc_queue;
};

class ServerContext : public BasicContext
{
public:
    ServerContext(size_t sid) : server_id_(sid), stat_req_ping_tot(0), stat_req_ping_resp_tot(0), stat_req_tc_tot(0), stat_req_tc_req_tot(0), stat_req_err_tot(0)
    {
    }
    ~ServerContext()
    {
    }
    size_t server_id_;
    size_t stat_req_ping_tot;
    size_t stat_req_ping_resp_tot;
    size_t stat_req_tc_tot;
    size_t stat_req_tc_req_tot;
    size_t stat_req_err_tot;

    void reset_stat()
    {
        stat_req_ping_tot = 0;
        stat_req_ping_resp_tot = 0;
        stat_req_tc_tot = 0;
        stat_req_tc_req_tot = 0;
        stat_req_err_tot = 0;
    }

    atomic_queue::AtomicQueueB2<erpc::MsgBuffer, std::allocator<erpc::MsgBuffer>, true, false, true> *forward_spsc_queue;
    erpc::MsgBuffer *req_backward_msgbuf_ptr;
};

class AppContext
{
public:
    AppContext()
    {
        int ret = hdr_init(1, 1000 * 1000 * 10, 3,
                           &latency_hist_);
        rmem::rt_assert(ret == 0, "hdr_init failed");
        for (size_t i = 0; i < FLAGS_client_num; i++)
        {
            client_contexts_.push_back(new ClientContext(i, i % FLAGS_server_forward_num, i % FLAGS_server_backward_num));
        }
        for (size_t i = 0; i < FLAGS_server_num; i++)
        {
            ServerContext *ctx = new ServerContext(i);
            ctx->forward_spsc_queue = client_contexts_[i]->forward_spsc_queue;
            ctx->req_backward_msgbuf_ptr = client_contexts_[i]->req_backward_msgbuf;
            server_contexts_.push_back(ctx);
        }
    }
    ~AppContext()
    {
        hdr_close(latency_hist_);

        for (auto &ctx : client_contexts_)
        {
            delete ctx;
        }
        for (auto &ctx : server_contexts_)
        {
            delete ctx;
        }
    }
    bool write_latency_and_reset(std::string filename)
    {

        FILE *fp = fopen(filename.c_str(), "w");
        if (fp == nullptr)
        {
            return false;
        }
        hdr_percentiles_print(latency_hist_, fp, 5, 10, CLASSIC);
        fclose(fp);
        hdr_reset(latency_hist_);
        return true;
    }

    uint32_t req_number_;

    std::vector<ClientContext *> client_contexts_;
    std::vector<ServerContext *> server_contexts_;

    hdr_histogram *latency_hist_;
};

void resize_bitmap(void *buf_, erpc::Rpc<erpc::CTransport> *rpc_, SPSC_QUEUE *consumer)
{
    TranscodeReq *req = static_cast<TranscodeReq *>(buf_);
    void *img_begin_ptr = req + 1;
    BITMAPFILEHEADER *bf = static_cast<BITMAPFILEHEADER *>(img_begin_ptr);
    img_begin_ptr = bf + 1;
    BITMAPINFOHEADER *bi = static_cast<BITMAPINFOHEADER *>(img_begin_ptr);
    img_begin_ptr = bi + 1;

    if (bf->bfType != 0x4d42 || bf->bfOffBits != 54 || bi->biSize != 40 ||
        bi->biBitCount != 24 || bi->biCompression != 0)
    {
        printf("req %u: invalid bitmap file\n", req->req.req_number);

        erpc::MsgBuffer resp_buf = rpc_->alloc_msg_buffer_or_die(sizeof(TranscodeReq));
        new (resp_buf.buf_) TranscodeReq(RPC_TYPE::RPC_TRANSCODE_RESP, req->req.req_number, 0);
        consumer->push(resp_buf);
        return;
    }
    LONG resize_biWidth = bi->biWidth * FLAGS_resize_factor;
    LONG resize_biHeight = bi->biHeight * FLAGS_resize_factor;
    int padding = bi->biWidth % 4;
    int padding_resize = resize_biWidth % 4;

    LONG resize_size = (resize_biWidth * sizeof(RGBTRIPLE) + padding_resize) * resize_biHeight + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    erpc::MsgBuffer resp_buf = rpc_->alloc_msg_buffer_or_die(sizeof(TranscodeReq) + resize_size);

    TranscodeReq *resp = new (resp_buf.buf_) TranscodeReq(RPC_TYPE::RPC_TRANSCODE_RESP, req->req.req_number, resize_size);

    void *resize_img_begin_ptr = resp + 1;
    BITMAPFILEHEADER *bf_resize = static_cast<BITMAPFILEHEADER *>(resize_img_begin_ptr);
    resize_img_begin_ptr = bf_resize + 1;

    BITMAPINFOHEADER *bi_resize = static_cast<BITMAPINFOHEADER *>(resize_img_begin_ptr);
    resize_img_begin_ptr = bi_resize + 1;

    BYTE *pix = static_cast<BYTE *>(img_begin_ptr);
    BYTE *resize_pix = static_cast<BYTE *>(resize_img_begin_ptr);

    *bf_resize = *bf;
    *bi_resize = *bi;

    bi_resize->biWidth = resize_biWidth;
    bi_resize->biHeight = resize_biHeight;

    bi_resize->biSizeImage = (resize_biWidth * sizeof(RGBTRIPLE) + padding_resize) * resize_biHeight;
    bf_resize->bfSize = resize_size;

    int width_length = bi->biWidth * sizeof(RGBTRIPLE) + padding;
    int resize_width_length = resize_biWidth * sizeof(RGBTRIPLE) + padding_resize;
    // temporary storage
    for (int i = 0; i < bi_resize->biHeight; i++)
    {
        for (int j = 0; j < bi_resize->biWidth; j++)
        {
            // calculate the corresponding coorinates in the original image
            int m = (i / FLAGS_resize_factor + 0.5); // +0.5 for rounding
            if (m > bi->biHeight - 1)
            { // limit the value
                m = bi->biHeight - 1;
            }
            int n = (j / FLAGS_resize_factor + 0.5);
            if (n > bi->biWidth - 1)
            {
                n = bi->biWidth - 1;
            }
            // pick the pixel value at the coordinate
            int tmp = m * width_length + n * sizeof(RGBTRIPLE);
            int tmp_size = i * resize_width_length + j * sizeof(RGBTRIPLE);

            resize_pix[tmp_size] = pix[tmp];
            resize_pix[tmp_size + 1] = pix[tmp + 1];
            resize_pix[tmp_size + 2] = pix[tmp + 2];
        }
        for (int j = 0; j < padding_resize; j++)
        {
            resize_pix[i * resize_width_length + bi_resize->biWidth * sizeof(RGBTRIPLE) + j] = 0;
        }
    }
    consumer->push(resp_buf);
}