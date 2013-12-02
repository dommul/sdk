/*

MEGA SDK - Client Access Engine Core Logic

(c) 2013 by Mega Limited, Wellsford, New Zealand

Author: mo
Bugfixing: js, mr

Applications using the MEGA API must present a valid application key
and comply with the the rules set forth in the Terms of Service.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#include "mega/transferslot.h"
#include "mega/node.h"
#include "mega/transfer.h"
#include "mega/megaclient.h"
#include "mega/command.h"
#include "mega/base64.h"
#include "mega/megaapp.h"
#include "mega/utils.h"

namespace mega {

TransferSlot::TransferSlot(Transfer* ctransfer)
{
	starttime = 0;
	progressreported = 0;
	progresscompleted = 0;
	lastdata = 0;

	fileattrsmutable = 0;

	reqs = NULL;
	pendingcmd = NULL;

	transfer = ctransfer;
	transfer->slot = this;

	connections = transfer->client->connections[transfer->type];

	reqs = new HttpReqXfer*[connections]();

	file = transfer->client->fsaccess->newfileaccess();

	slots_it = transfer->client->tslots.end();
}

// delete slot and associated resources, but keep transfer intact
TransferSlot::~TransferSlot()
{
	transfer->slot = NULL;

	if (slots_it != transfer->client->tslots.end()) transfer->client->tslots.erase(slots_it);

	if (pendingcmd) pendingcmd->cancel();

	if (file)
	{
		delete file;

		if (transfer->type == GET && transfer->localfilename.size()) transfer->client->fsaccess->unlinklocal(&transfer->localfilename);
	}

	while (connections--) delete reqs[connections];
	delete[] reqs;
}

// abort all HTTP connections
void TransferSlot::disconnect()
{
	for (int i = connections; i--; ) if (reqs[i]) reqs[i]->disconnect();
}


// coalesce block macs into file mac
int64_t TransferSlot::macsmac(chunkmac_map* macs)
{
	byte mac[SymmCipher::BLOCKSIZE] = { 0 };

	for (chunkmac_map::iterator it = macs->begin(); it != macs->end(); it++)
	{
		SymmCipher::xorblock(it->second.mac,mac);
		transfer->key.ecb_encrypt(mac);
	}

	macs->clear();

	uint32_t* m = (uint32_t*)mac;

	m[0] ^= m[1];
	m[1] = m[2]^m[3];

	return *(int64_t*)mac;
}

// file transfer state machine
void TransferSlot::doio(MegaClient* client)
{
	if (!tempurl.size()) return;

	time_t backoff = 0;
	m_off_t p = 0;

	for (int i = connections; i--; )
	{
		if (reqs[i])
		{
			switch (reqs[i]->status)
			{
				case REQ_INFLIGHT:
					p += reqs[i]->transferred(client);
					break;

				case REQ_SUCCESS:
					lastdata = client->waiter->ds;

					progresscompleted += reqs[i]->size;

					if (transfer->type == PUT)
					{
						// completed put transfers are signalled through the return of the upload token
						if (reqs[i]->in.size())
						{
							if (reqs[i]->in.size() == NewNode::UPLOADTOKENLEN*4/3)
							{
								if (Base64::atob(reqs[i]->in.data(),ultoken,NewNode::UPLOADTOKENLEN+1) == NewNode::UPLOADTOKENLEN)
								{
									memcpy(transfer->filekey,transfer->key.key,sizeof transfer->key.key);
									((int64_t*)transfer->filekey)[2] = transfer->ctriv;
									((int64_t*)transfer->filekey)[3] = macsmac(&transfer->chunkmacs);
									SymmCipher::xorblock(transfer->filekey+SymmCipher::KEYLENGTH,transfer->filekey);

									return transfer->complete();
								}
							}

							// fail with returned error
							return transfer->failed((error)atoi(reqs[i]->in.c_str()));
						}
					}
					else
					{
						reqs[i]->finalize(file,&transfer->key,&transfer->chunkmacs,transfer->ctriv,0,-1);

						if (progresscompleted == transfer->size)
						{
							// verify meta MAC
							if (macsmac(&transfer->chunkmacs) == transfer->metamac) return transfer->complete();
							else return transfer->failed(API_EKEY);
						}
					}

					reqs[i]->status = REQ_READY;
					break;

				case REQ_FAILURE:
					reqs[i]->status = REQ_PREPARED;
					break;

					if (reqs[i]->httpstatus == 509)
					{
						client->app->transfer_limit(transfer);

						// fixed ten-minute retry intervals
						backoff = 6000;
					}
					else return transfer->failed(API_ETEMPUNAVAIL);

				default:;
			}
		}

		if (!reqs[i] || reqs[i]->status == REQ_READY)
		{
			m_off_t npos = ChunkedHash::chunkceil(transfer->pos);

			if (npos > transfer->size) npos = transfer->size;

			if (npos > transfer->pos || !transfer->size)
			{
				if (!reqs[i]) reqs[i] = (transfer->type == PUT) ? (HttpReqXfer*)new HttpReqUL() : (HttpReqXfer*)new HttpReqDL();

				reqs[i]->prepare(file,tempurl.c_str(),&transfer->key,&transfer->chunkmacs,transfer->ctriv,transfer->pos,npos);
				reqs[i]->status = REQ_PREPARED;
				transfer->pos = npos;
			}
			else if (reqs[i]) reqs[i]->status = REQ_DONE;
		}

		if (reqs[i] && reqs[i]->status == REQ_PREPARED) reqs[i]->post(client);
	}

	p += progresscompleted;

	if (p != progressreported)
	{
		progressreported = p;
		lastdata = client->waiter->ds;

		progress();
	}

	if (client->waiter->ds-lastdata >= XFERTIMEOUT) return transfer->failed(API_EFAILED);
	else
	{
		if (!backoff)
		{
			// no other backoff: check again at XFERMAXFAIL
			backoff = XFERTIMEOUT-(client->waiter->ds-lastdata);
		}

		transfer->bt.backoff(client->waiter->ds,backoff);
	}
}

// transfer progress notification to app and related files
void TransferSlot::progress()
{
	transfer->client->app->transfer_update(transfer);

	for (file_list::iterator it = transfer->files.begin(); it != transfer->files.end(); it++) (*it)->progress();
}

} // namespace
