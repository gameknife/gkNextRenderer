#ifndef ShadingFunc_glsl

uint FetchMaterialId(in NodeProxy node, uint offset)
{
	uint materialIndex = 0;
	switch (offset) {
		case 0:
			materialIndex = node.matId[0];
			break;
		case 1:
			materialIndex = node.matId[1];
			break;
		case 2:
			materialIndex = node.matId[2];
			break;
		case 3:
			materialIndex = node.matId[3];
			break;
		case 4:
			materialIndex = node.matId[4];
			break;
		case 5:
			materialIndex = node.matId[5];
			break;
		case 6:
			materialIndex = node.matId[6];
			break;
		case 7:
			materialIndex = node.matId[7];
			break;
		case 8:
			materialIndex = node.matId[8];
			break;
		case 9:
			materialIndex = node.matId[9];
			break;
		case 10:
			materialIndex = node.matId[10];
			break;
		case 11:
			materialIndex = node.matId[11];
			break;
		case 12:
			materialIndex = node.matId[12];
			break;
		case 13:
			materialIndex = node.matId[13];
			break;
		case 14:
			materialIndex = node.matId[14];
			break;
		case 15:
			materialIndex = node.matId[15];
			break;
	}
	return materialIndex;
}

#define ShadingFunc_glsl
#endif