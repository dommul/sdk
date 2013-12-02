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

#include "mega/http.h"
#include "mega/megaclient.h"

namespace mega {

void HttpReq::post(MegaClient* client, const char* data, unsigned len)
{
	httpio = client->httpio;
	bufpos = 0;

	httpio->post(this,data,len);
}

void HttpReq::disconnect()
{
	if (httpio) httpio->cancel(this);
}

HttpReq::HttpReq(int b)
{
	binary = b;

	status = REQ_READY;
	buf = NULL;

	httpio = NULL;
	httpiohandle = NULL;
	out = &outbuf;
}

HttpReq::~HttpReq()
{
	if (httpio) httpio->cancel(this);

	delete[] buf;
}

void HttpReq::setreq(const char* u, contenttype t)
{
	posturl = u;
	type = t;
}

// add data to fixed or variable buffer
void HttpReq::put(void* data, unsigned len)
{
	if (buf)
	{
		if (bufpos+len > buflen) len = buflen-bufpos;

		memcpy(buf+bufpos,data,len);
		bufpos += len;
	}
	else in.append((char*)data,len);
}

// make space for receiving data; adjust len if out of space
byte* HttpReq::reserveput(unsigned* len)
{
	if (buf)
	{
		if (bufpos+*len > buflen) *len = buflen-bufpos;
		return buf+bufpos;
	}
	else
	{
		if (bufpos+*len > in.size()) in.resize(bufpos+*len);
		*len = in.size()-bufpos;
		return (byte*)in.data()+bufpos;
	}
}

// confirm the receipt of data
void HttpReq::completeput(unsigned len)
{
	bufpos += len;
}

// number of bytes transferred in this request
m_off_t HttpReq::transferred(MegaClient*)
{
	if (buf) return bufpos;
	else return in.size();
}

// prepare file chunk download
void HttpReqDL::prepare(FileAccess* fa, const char* tempurl, SymmCipher* key, chunkmac_map* macs, uint64_t ctriv, m_off_t pos, m_off_t npos)
{
	char urlbuf[256];

	snprintf(urlbuf,sizeof urlbuf,"%s/%" PRIu64 "-%" PRIu64,tempurl,pos,npos-1);
	setreq(urlbuf,REQ_BINARY);

	dlpos = pos;
	size = (unsigned)(npos-pos);

	if (!buf || buflen != size)
	{
		// (re)allocate buffer
		if (buf) delete[] buf;

		buf = new byte[(size+SymmCipher::BLOCKSIZE-1)&-SymmCipher::BLOCKSIZE];
		buflen = size;
	}
}

// decrypt, mac and write downloaded chunk
void HttpReqDL::finalize(FileAccess* fa, SymmCipher* key, chunkmac_map* macs, uint64_t ctriv, m_off_t startpos, m_off_t endpos)
{
	byte mac[SymmCipher::BLOCKSIZE] = { 0 };

	key->ctr_crypt(buf,bufpos,dlpos,ctriv,mac,0);

	unsigned skip;
	unsigned prune;

	if (endpos == -1) skip = prune = 0;
	else
	{
		if (startpos > dlpos) skip = (unsigned)(startpos-dlpos);
		else skip = 0;

		if (dlpos+bufpos > endpos) prune = (unsigned)(dlpos+bufpos-endpos);
		else prune = 0;
	}

	fa->fwrite(buf+skip,bufpos-skip-prune,dlpos+skip);

	memcpy((*macs)[dlpos].mac,mac,sizeof mac);
}

// prepare chunk for uploading: mac and encrypt
void HttpReqUL::prepare(FileAccess* fa, const char* tempurl, SymmCipher* key, chunkmac_map* macs, uint64_t ctriv, m_off_t pos, m_off_t npos)
{
	byte mac[SymmCipher::BLOCKSIZE] = { 0 };
	char buf[256];

	snprintf(buf,sizeof buf,"%s/%" PRIu64,tempurl,pos);
	setreq(buf,REQ_BINARY);

	size = (unsigned)(npos-pos);

	// FIXME: check return value and abort upload in case file read fails
	fa->fread(out,size,(-(int)size)&(SymmCipher::BLOCKSIZE-1),pos);

	key->ctr_crypt((byte*)out->data(),size,pos,ctriv,mac,1);

	memcpy((*macs)[pos].mac,mac,sizeof mac);

	// unpad for POSTing
	out->resize(size);
}

// number of bytes sent in this request
m_off_t HttpReqUL::transferred(MegaClient* client)
{
	if (httpiohandle) return client->httpio->postpos(httpiohandle);

	return 0;
}

} // namespace
