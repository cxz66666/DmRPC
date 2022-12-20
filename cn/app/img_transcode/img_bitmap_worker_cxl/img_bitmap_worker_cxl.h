#pragma once
#include <hdr/hdr_histogram.h>
#include "atomic_queue/atomic_queue.h"
#include "../img_transcode_commons.h"
#include "bmp.h"
#include "phmap.h"
#include <sys/mman.h>

using SPSC_QUEUE = atomic_queue::AtomicQueueB2<erpc::MsgBuffer, std::allocator<erpc::MsgBuffer>, true, false, false>;

DEFINE_double(resize_factor, 0.5, "the resize factor, must be between 0 and 1");
DEFINE_uint64(worker_num, 1, "worker thread number");

const size_t batch_size = 4;

void *cxl_req_msgbuf[kAppMaxCXLSession][kAppMaxConcurrency];
void *cxl_resp_msgbuf[kAppMaxCXLSession][kAppMaxConcurrency];

std::vector<std::ifstream> cxls_(kAppMaxCXLSession);
std::vector<void *> cxls_addr_(kAppMaxCXLSession, nullptr);
std::vector<SPSC_QUEUE *> forward_spsc_queue(kAppMaxCXLSession, nullptr);
SPSC_QUEUE *backward_spsc_queue;

phmap::flat_hash_map<uint32_t, uint32_t> rpc_id_to_index;
volatile uint32_t rpc_connect_number = 0;
class ClientContext : public BasicContext
{

public:
    ClientContext(size_t cid, size_t sid, size_t rid) : client_id_(cid), server_sender_id_(sid), server_receiver_id_(rid)
    {
    }
    ~ClientContext()
    {
    }
    erpc::MsgBuffer req_backward_msgbuf[kAppMaxCXLSession][kAppMaxConcurrency];

    erpc::MsgBuffer resp_backward_msgbuf[kAppMaxCXLSession][kAppMaxConcurrency];

    size_t client_id_;
    size_t server_sender_id_;
    size_t server_receiver_id_;
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

    erpc::MsgBuffer (*req_backward_msgbuf_ptr)[kAppMaxConcurrency];
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
            auto *ctx = new ServerContext(i);
            ctx->req_backward_msgbuf_ptr = client_contexts_[i]->req_backward_msgbuf;
            server_contexts_.push_back(ctx);
        }
        backward_spsc_queue = new SPSC_QUEUE(kAppMaxConcurrency * kAppMaxCXLSession);
        for (size_t i = 0; i < kAppMaxCXLSession; i++)
        {
            forward_spsc_queue[i] = new SPSC_QUEUE(kAppMaxConcurrency);
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
    bool write_latency_and_reset(const std::string &filename)
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

    std::vector<ClientContext *> client_contexts_;
    std::vector<ServerContext *> server_contexts_;

    hdr_histogram *latency_hist_;
};

bool resize_bitmap(void *input_buf_, void *output_buf_)
{
    void *img_begin_ptr = input_buf_;
    auto *bf = static_cast<BITMAPFILEHEADER *>(img_begin_ptr);
    img_begin_ptr = bf + 1;
    BITMAPINFOHEADER *bi = static_cast<BITMAPINFOHEADER *>(img_begin_ptr);
    img_begin_ptr = bi + 1;

    if (bf->bfType != 0x4d42 || bf->bfOffBits != 54 || bi->biSize != 40 ||
        bi->biBitCount != 24 || bi->biCompression != 0)
    {
        printf("invalid bitmap file\n");
        return false;
    }
    LONG resize_biWidth = bi->biWidth * FLAGS_resize_factor;
    LONG resize_biHeight = bi->biHeight * FLAGS_resize_factor;
    int padding = bi->biWidth % 4;
    int padding_resize = resize_biWidth % 4;

    LONG resize_size = (resize_biWidth * sizeof(RGBTRIPLE) + padding_resize) * resize_biHeight + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    void *resize_img_begin_ptr = output_buf_;
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
    return true;
}

void create_session_and_join(PingReq *req_ping)
{
    rmem::rt_assert(static_cast<size_t>(req_ping->req.req_number) < kAppMaxCXLSession, "cxl sessions out of range");
    CxlParam param = req_ping->cxl_param;

    printf("filename %s, session_id %u\n", param.filename, req_ping->req.req_number);
    std::string filename(param.filename);
    uint32_t now_connect_number = rpc_connect_number++;
    rpc_id_to_index[req_ping->req.req_number] = now_connect_number;

    for (size_t i = 0; i < kAppMaxConcurrency; i++)
    {
        cxl_req_msgbuf[now_connect_number][i] = malloc(param.file_size_aligned);
        cxl_resp_msgbuf[now_connect_number][i] = malloc(param.file_size_aligned);
    }
    std::ifstream ifs(filename, std::ios::binary | std::ios::in);

    cxls_[now_connect_number] = std::move(ifs);
    FILE *fptr = fopen(filename.c_str(), "rb");

    void *tmp = mmap(NULL, req_ping->cxl_param.total_size, PROT_READ, MAP_SHARED, fileno(fptr), 0);
    if (tmp == NULL || tmp == MAP_FAILED)
    {
        printf("mmap failed\n");
        exit(1);
    }
    cxls_addr_[now_connect_number] = tmp;
}

void worker_ping_thread(erpc::MsgBuffer req_msg)
{
    create_session_and_join(reinterpret_cast<PingReq *>(req_msg.buf_));
    auto *req = reinterpret_cast<CommonReq *>(req_msg.buf_);
    req->type = RPC_TYPE::RPC_PING_RESP;
    backward_spsc_queue->push(req_msg);
}