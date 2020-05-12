#include "internal.h"

static inline bool _addressIsLinear(const void* addr)
{
	u32 vaddr = (u32)addr;
	return vaddr >= __ctru_linear_heap && vaddr < (__ctru_linear_heap + __ctru_linear_heap_size);
}

static void _configAttribBuffer(uint8_t id, uint64_t format, uint8_t stride, uint8_t count, uint32_t padding)
{
	uint32_t param[2];

	if(id > 0xB) return;

	while(padding > 0)
	{
		if(padding < 4) {
			format |= (0xB + padding) << (count * 4);
			padding = 0;
		} else {
			format |= 0xF << (count * 4);
			padding -= 4;
		}
		count++;
	}

	param[0] = format & 0xFFFFFFFF;
	param[1] = (count << 28) | ((stride & 0xFF) << 16) | ((format >> 32) & 0xFFFF);

	GPUCMD_AddIncrementalWrites(GPUREG_ATTRIBBUFFER0_CONFIG1 + (id * 0x03), param, 0x02);
}

//Check if we have enough space in the current geometry buffer
static bool _checkBufferLimit(GLsizei size)
{
	if(pglState->geometryBufferOffset + size > GEOMETRY_BUFFER_SIZE)
		return false;
	else
		return true;
}

static void* _bufferArray(const void *data, GLsizei size)
{
	void *cache = pglState->geometryBuffer[pglState->geometryBufferCurrent] + pglState->geometryBufferOffset;

	if(data != NULL)
		memcpy(cache, data, size);

	pglState->geometryBufferOffset += size;

	return cache;
}

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid * pointer)
{
	switch(type)
	{
		case GL_SHORT:
			pglState->vertexArrayPointer.type = GPU_SHORT;
			pglState->vertexArrayPointer.stride = size * 2;
			break;
		case GL_FLOAT:
			pglState->vertexArrayPointer.type = GPU_FLOAT;
			pglState->vertexArrayPointer.stride = size * 4;
			break;
		default:
			printf("%s: unimplemented type: %i\n", __FUNCTION__, type);
			break;
	}

	if(stride)
	{
		pglState->vertexArrayPointer.padding = (stride - pglState->vertexArrayPointer.stride) / 4;
		pglState->vertexArrayPointer.stride = stride;
	}

	pglState->vertexArrayPointer.pointer = pointer;
	pglState->vertexArrayPointer.size = size;
}

void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid * pointer)
{
	switch(type)
	{
		case GL_BYTE:
			pglState->colorArrayPointer.type = GPU_BYTE;
			pglState->colorArrayPointer.stride = size * 1;
			break;
		case GL_UNSIGNED_BYTE:
			pglState->colorArrayPointer.type = GPU_UNSIGNED_BYTE;
			pglState->colorArrayPointer.stride = size * 1;
			break;
		case GL_SHORT:
			pglState->colorArrayPointer.type = GPU_SHORT;
			pglState->colorArrayPointer.stride = size * 2;
			break;
		case GL_FLOAT:
			pglState->colorArrayPointer.type = GPU_FLOAT;
			pglState->colorArrayPointer.stride = size * 4;
			break;
		default:
			printf("%s: unimplemented type: %i\n", __FUNCTION__, type);
			return;
	}

	if(stride)
	{
		pglState->colorArrayPointer.padding = (stride - pglState->colorArrayPointer.stride) / 4;
		pglState->colorArrayPointer.stride  = stride;
	}

	pglState->colorArrayPointer.pointer = pointer;
	pglState->colorArrayPointer.size = size;
}

void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer)
{
	AttribPointer *texCoordArray = &pglState->texCoordArrayPointer[pglState->texUnitActiveClient];

	switch(type)
	{
		case GL_SHORT:
			texCoordArray->type = GPU_SHORT;
			texCoordArray->stride = size * 2;
			break;
		case GL_FLOAT:
			texCoordArray->type = GPU_FLOAT;
			texCoordArray->stride = size * 4;
			break;
		default:
			printf("%s: unimplemented type: %i\n", __FUNCTION__, type);
			return;
	}

	if(stride)
	{
		texCoordArray->padding = (stride - texCoordArray->stride) / 4;
		texCoordArray->stride = stride;
	}

	texCoordArray->pointer = pointer;
	texCoordArray->size = size;
}

void glEnableClientState(GLenum array)
{
	switch(array)
	{
		case GL_VERTEX_ARRAY:
			pglState->vertexArrayState = GL_TRUE;
			break;
		case GL_COLOR_ARRAY:
			pglState->colorArrayState = GL_TRUE;
			break;
		case GL_TEXTURE_COORD_ARRAY:
			pglState->texCoordArrayState[pglState->texUnitActive] = GL_TRUE;
			break;
	}
}

void glDisableClientState(GLenum array)
{
	switch(array)
	{
		case GL_VERTEX_ARRAY:
			pglState->vertexArrayState = GL_FALSE;
			break;
		case GL_COLOR_ARRAY:
			pglState->colorArrayState = GL_FALSE;
			break;
		case GL_TEXTURE_COORD_ARRAY:
			pglState->texCoordArrayState[pglState->texUnitActive] = GL_FALSE;
			break;
	}
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
	if(pglState->vertexArrayState == GL_FALSE)
		return;

	glDrawRangeElements(mode, first, count, count, GL_UNSIGNED_SHORT, NULL);
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
	if(type != GL_UNSIGNED_SHORT || pglState->vertexArrayState == GL_FALSE)
		return;

	uint32_t end = 0;

	for(int i = 0; i < count; i++)
	{
			if(end < ((GLushort*)indices)[i])
				end = ((GLushort*)indices)[i];
	}

	end++;

	glDrawRangeElements( mode, 0, end, count, type, indices );
}

void glDrawRangeElements( GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices )
{
	if(type != GL_UNSIGNED_SHORT || pglState->vertexArrayState == GL_FALSE)
		return;

	void *arrayCache = NULL;

	uint8_t  bufferCount 			= 0;
	uint8_t  attributesCount		= 2;
	uint16_t attributesFixedMask 	= 0x00;
	uint64_t attributesFormat 		= 0x00;
	uint64_t attributesPermutation  = 0x10;

	AttribPointer *vertexArray 	    = &pglState->vertexArrayPointer;
	AttribPointer *colorArray 		= &pglState->colorArrayPointer;
	AttribPointer *texCoordArray 	= pglState->texCoordArrayPointer;

	if(_checkBufferLimit((end * 12 * sizeof(GLfloat)) + (sizeof(GLushort) * count)) == false)
		glFlush();

	_stateFlush();

	if(_addressIsLinear(vertexArray->pointer))
		arrayCache = (void*)vertexArray->pointer;
	else
		arrayCache = _bufferArray(vertexArray->pointer, vertexArray->stride * end);

	attributesFormat |= GPU_ATTRIBFMT(0, vertexArray->size, vertexArray->type);
	_picaAttribBufferOffset(bufferCount, (uint32_t)arrayCache - __ctru_linear_heap);
	_configAttribBuffer(bufferCount, 0x0, vertexArray->stride, 1, vertexArray->padding);

	bufferCount++;

	if(pglState->colorArrayState == GL_TRUE)
	{
		if(_addressIsLinear(colorArray->pointer))
			arrayCache = (void*)colorArray->pointer;
		else
			arrayCache = _bufferArray(colorArray->pointer, colorArray->stride * end);

		attributesFormat |= GPU_ATTRIBFMT(1, colorArray->size, colorArray->type);
		_picaAttribBufferOffset(bufferCount, (uint32_t)arrayCache - __ctru_linear_heap);
		_configAttribBuffer(bufferCount, 0x1, colorArray->stride, 1, colorArray->padding);

		bufferCount++;
	}
	else
	{
		attributesFixedMask |= 1 << 1;
		attributesFormat |= GPU_ATTRIBFMT(1, 4, GPU_FLOAT);

		GPUCMD_AddWrite(GPUREG_FIXEDATTRIB_INDEX, 1);
		_picaFixedAttribute(pglState->currentColor.r, pglState->currentColor.g, pglState->currentColor.b, pglState->currentColor.a);
	}

	if( (pglState->texCoordArrayState[0] == GL_TRUE) && (pglState->texUnitState[0] == GL_TRUE) )
	{
		attributesPermutation |= 0x2 << (attributesCount * 4);

		if(_addressIsLinear(texCoordArray[0].pointer))
			arrayCache = (void*)texCoordArray[0].pointer;
		else
			arrayCache = _bufferArray(texCoordArray[0].pointer, texCoordArray[0].stride * end);

		attributesFormat |= GPU_ATTRIBFMT(2, texCoordArray[0].size, texCoordArray[0].type);
		_picaAttribBufferOffset(bufferCount, (uint32_t)arrayCache - __ctru_linear_heap);
		_configAttribBuffer(bufferCount, 0x2, texCoordArray[0].stride, 1, texCoordArray[0].padding);

		attributesCount++;
		bufferCount++;
	}
	else
	{
		attributesFixedMask |= 1 << 2;
		attributesFormat |= GPU_ATTRIBFMT(2, 2, GPU_FLOAT);

		GPUCMD_AddWrite(GPUREG_FIXEDATTRIB_INDEX, 2);
		_picaFixedAttribute(1, 1, 0, 0);
		attributesCount++;
	}

	if( (pglState->texCoordArrayState[1] == GL_TRUE) && (pglState->texUnitState[1] == GL_TRUE) )
	{	
		attributesPermutation |= 0x3 << (attributesCount * 4);
		arrayCache = _bufferArray(texCoordArray[1].pointer, texCoordArray[1].stride * end);
		
		attributesFormat |= GPU_ATTRIBFMT(3, texCoordArray[1].size, texCoordArray[1].type);
		_picaAttribBufferOffset(bufferCount, (uint32_t)arrayCache - __ctru_linear_heap);
		_configAttribBuffer(bufferCount, 0x3, texCoordArray[1].stride, 1, texCoordArray[1].padding);

		attributesCount++;
		bufferCount++;
	}

	_picaAttribBuffersFormat(attributesFormat, attributesFixedMask, attributesPermutation, bufferCount);

	GPU_Primitive_t primitive_type;

	switch(mode)
	{
		case GL_TRIANGLES: 
			primitive_type = GPU_TRIANGLES; 
			break;
		case GL_TRIANGLE_FAN:
			primitive_type = GPU_TRIANGLE_FAN; 
			break;
		case GL_TRIANGLE_STRIP:
			primitive_type = GPU_TRIANGLE_STRIP;
			break;
		default:
			primitive_type = GPU_TRIANGLES;
	}
	
	if(indices)
	{
		uint16_t* index_array  = _bufferArray(indices, sizeof(uint16_t) * count);

		_picaDrawElements(primitive_type, (uint32_t)index_array - __ctru_linear_heap, count);
	}
	else
	{
		_picaDrawArray(primitive_type, start, count);
	}

	if(++pglState->batchedDraws > MAX_BATCHED_DRAWS)
		glFlush();
}