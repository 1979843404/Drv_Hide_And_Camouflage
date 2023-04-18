#include"Head.h"

KEVENT WaitWorkItem;
PDRIVER_OBJECT ShellDrv = NULL;
BOOLEAN IsWDF = FALSE;

//����ShellDriverEntry����
NTSTATUS Shim(PShellContext PSContext) {
	NTSTATUS s = STATUS_SUCCESS;
	ShellDriverEntry(PSContext->DrvObj, PSContext->PSTR);
	KeSetEvent(&WaitWorkItem,0,FALSE);
	return s;

}

//ͨ���������ֻ�ȡ��ַ
PUCHAR GetDllBase(PUCHAR PDllName) {
	ANSI_STRING DllNameA;
	UNICODE_STRING DllNameU = { 0 };
	RtlInitAnsiString(&DllNameA, PDllName);
	RtlAnsiStringToUnicodeString(&DllNameU, &DllNameA, TRUE);
	PLIST_ENTRY64 PDriverSection = PsLoadedModuleList->Blink;

	PUCHAR PDriverSectionByte = NULL;
	PUCHAR ReturnBase = NULL;
	PUNICODE_STRING BaseDllName = NULL;
	while (PDriverSection != PsLoadedModuleList) {
		PDriverSectionByte = (PUCHAR)PDriverSection;
		BaseDllName = (PUNICODE_STRING)(PDriverSectionByte + BaseDllNameOffset);
		if (RtlEqualUnicodeString(BaseDllName, &DllNameU, TRUE)) {
			ReturnBase = *((PULONG64)(PDriverSectionByte + DllBaseOffset));
			break;
		}
		else {
			PDriverSection = PDriverSection->Blink;
		}
	}
	RtlFreeUnicodeString(&DllNameU);
	return ReturnBase;
}

//����ҳ���д
VOID SetWrite(ULONG64 va) {
	PPT_ENTRY_4KB ppte = NULL;
	PTE_HIERARCHY context = { 0 };
	ULONG64 a = *(PULONG64)va;
	MiFillPteHierarchy(va, &context);
	ppte = context.pte;
	ppte->Fields.Write = 1;

}

ULONG64 WdfR0() {
	return 0;
}
//���IAT
BOOLEAN MakeIAT(PUCHAR DllBase) {
	PMyIID Piid = NULL;
	PUCHAR ImportDllBase = NULL;
	ULONG32 ImportSize = 0;
	PUCHAR PDllName = NULL;
	//DbgBreakPoint();
	PUCHAR ImportVirtualAddress = RtlImageDirectoryEntryToData(DllBase, 1, 1, &ImportSize);
	int iidSize = sizeof(MyIID);

	PULONG64 PThisIATEOffset = 0;
	PULONG64 PThisITEOffset = 0;
	NTSTATUS status = STATUS_SUCCESS;
	Piid = (PMyIID)ImportVirtualAddress;
	for (int i = 0; i < ImportSize; i += iidSize) {
		if (Piid->d == 0)
			break;//ȫ��������
		PDllName = (PUCHAR)(DllBase + Piid->d);
		if (0 == memcmp(PDllName, WDF, 10)) {//WDF ��Ҫ�޸�
			IsWDF = TRUE;
			PThisIATEOffset = DllBase + Piid->e;
			while (*PThisIATEOffset != 0) {
				SetWrite(PThisIATEOffset);
				*PThisIATEOffset = WdfR0;
				PThisIATEOffset++;
			}
			Piid++;
			continue;
		}
		ImportDllBase = GetDllBase(PDllName);//get ImportDllBase
		PThisIATEOffset = DllBase + Piid->e;
		PThisITEOffset = DllBase + Piid->a;
		while (*PThisIATEOffset != 0 && *PThisITEOffset != 0) {
			SetWrite(PThisIATEOffset);
			status = MiSnapThunk(ImportDllBase, DllBase, PThisITEOffset, PThisIATEOffset, 0);
			if (status != STATUS_SUCCESS) {
				DbgPrint("error!\n");
				return FALSE;
			}
			PThisITEOffset++;
			PThisIATEOffset++;
		}
		Piid++;
	}
	return TRUE;
}

//����ǩ��ǿ���Իص�
ULONG64 MySeValidateImageHeader() {
	return 0;
}

struct _WDF_BIND_INFO {
	ULONG32 Szie;
	UCHAR RZ[4];
	ULONG64 Component;
	UCHAR Version[0xc];
	ULONG32 FuncCount;
	ULONG64 FuncTable;
	ULONG64 Module;
};
//������������
VOID LoadDrv(PWCHAR DrvPath) {

	int un = 0;
	PUCHAR PDriverSection = NULL;
	PUCHAR Section = NULL;
	PUCHAR DllBase = NULL;
	UNICODE_STRING Path;
	UNICODE_STRING Out;
	UNICODE_STRING Out14[14];
	UNICODE_STRING String1;
	PKTHREAD thread = NULL;
	//��ʼ���¼�
	KeInitializeEvent(&WaitWorkItem, SynchronizationEvent, FALSE);

	//��������ǩ��ǿ��
	CIFun = *Pqword_14040EF40;
	DbgPrint("PSeValidateImageHeader here %p\n", Pqword_14040EF40);
	*Pqword_14040EF40 = MySeValidateImageHeader;
	//ӳ������
	RtlInitUnicodeString(&Path, DrvPath);
	NTSTATUS s0 = MiGenerateSystemImageNames(&Path, 0, 0, &Out, Out14, &String1);
	thread = MmAcquireLoadLock();
	NTSTATUS s1 = MiObtainSectionForDriver(&String1, &Path, 0, 0, &PDriverSection);
	MmReleaseLoadLock(thread);
	if (s1 != STATUS_SUCCESS) {
		DbgPrint("error code:%X\n", s1);
		return;
	}
	Section = *(PULONG64)(PDriverSection + SectionOffset);
	DllBase = MiGetSystemAddressForImage(Section, 0, &un);

	NTSTATUS s2 = MiMapSystemImage(Section, DllBase);
	//�ָ�����ǩ��ǿ��
	*Pqword_14040EF40 = CIFun;

	//��ȡDriverEntry
	PIMAGE_NT_HEADERS Head = RtlImageNtHeader(DllBase);
	PUCHAR Headd = (PUCHAR)Head;
	int* p = NULL;
	p = Headd + 0x28;//IMAGE_NT_HEADERS.OptionalHeader.AddressOfEntryPoint
	PULONG64 c = &ShellDriverEntry;
	*c = DllBase + *p;


	//�޸�IAT
	if (!MakeIAT(DllBase)) {
		return;
	}
	

	//�޸�_security_cookie
	int size = 0;
	PULONG64 ConfigAdd = 0;
	PULONG64 P_security_cookieAddress = NULL;
	ConfigAdd = RtlImageDirectoryEntryToData(DllBase, 1, 0xA, &size);
	P_security_cookieAddress = ConfigAdd[0xb];
	SetWrite(P_security_cookieAddress);
	*P_security_cookieAddress = 1;//������ģ�����һ��Ҫ��

	
	//�޸�WDF(��WDF��Ҫ�޸���WDM����)
	struct _WDF_BIND_INFO* PWdfBindInfo = ((ULONG64)P_security_cookieAddress) + 0x10;
	PULONG64 PWdfFunctions = PWdfBindInfo->FuncTable;
	PULONG64 PWdfDriverGlobals = NULL;
	if (IsWDF == TRUE) {
		*PWdfFunctions = (ULONG64)ExAllocatePool(NonPagedPool, 0x1000);
		if ((*PWdfFunctions) == NULL) {
			return;
		}
		memset(*PWdfFunctions, 0, 0x1000);
		PWdfDriverGlobals = ((ULONG64)PWdfFunctions) + 8;
		*PWdfDriverGlobals = ExAllocatePool(NonPagedPool, 0x100);
		if (*PWdfDriverGlobals == NULL) {
			ExFreePool(*PWdfFunctions);
			return;
		}memset(*PWdfDriverGlobals, 0, 0x100);
	}

	//driverEntry
	DbgPrint("DllBase:%p\n", DllBase);
	ShellContext SContext = { 0 };
	SContext.DrvObj = FindNotDeviceDriver();
	ULONG64 OldDriverUnLoad = SContext.DrvObj->DriverUnload;//����һ��DriverUnLoad
	WORK_QUEUE_ITEM WorkItem = {0};
	WorkItem.WorkerRoutine = Shim;
	WorkItem.Parameter = &SContext;
	WorkItem.List.Flink = 0i64;
	ExQueueWorkItem(&WorkItem, DelayedWorkQueue);
	//��һ��
	KeWaitForSingleObject(&WaitWorkItem, Executive, KernelMode, FALSE, NULL);

	//��ԭDriverUnLoad
	SContext.DrvObj->DriverUnload = OldDriverUnLoad;

	//�ύ�ٳ��豸
	IopReadyDeviceObjects(SContext.DrvObj);

	//�ͷ�
	if (IsWDF == TRUE) {
		ExFreePool(*PWdfFunctions);
		ExFreePool(*PWdfDriverGlobals);
		*PWdfFunctions = 0;
		*PWdfDriverGlobals = 0;
	}
	
}

//-------------------------------------------����αװ
//����ע����� ���ض�Ӧע�����
HANDLE CreateRegistry(PWCHAR ODrvPath, PWCHAR ServiceName) {
	NTSTATUS status = STATUS_SUCCESS;
	OBJECT_ATTRIBUTES objAttrs = { 0 };
	UNICODE_STRING SerRegistryPath = { 0 }, SerName = { 0 }, RegUnicodeString = { 0 };
	HANDLE hReg = NULL;
	ULONG64 Out = 0;
	UNICODE_STRING ImagePathUn = { 0 }, DisplayNameUn = { 0 }, ErrorControlUn = { 0 }, StartUn = { 0 }, TypeUn = { 0 };
	RtlInitUnicodeString(&DisplayNameUn, L"DisplayName");
	RtlInitUnicodeString(&ImagePathUn, L"ImagePath");
	RtlInitUnicodeString(&ErrorControlUn, L"ErrorControl");
	RtlInitUnicodeString(&StartUn, L"Start");
	RtlInitUnicodeString(&TypeUn, L"Type");
	ULONG64 EC = 1, Str = 3, Typ = 1;
	RtlInitUnicodeString(&SerRegistryPath, ServiceRegistryPath);
	RtlInitUnicodeString(&SerName, ServiceName);
	RegUnicodeString.Buffer = ExAllocatePool(NonPagedPool, (ULONG64)SerRegistryPath.MaximumLength + (ULONG64)SerName.MaximumLength);
	if (RegUnicodeString.Buffer == NULL) {
		return 0;
	}
	//DbgBreakPoint();
	memset(RegUnicodeString.Buffer, 0, (ULONG64)SerRegistryPath.MaximumLength + (ULONG64)SerName.MaximumLength);
	memcpy(RegUnicodeString.Buffer, SerRegistryPath.Buffer, SerRegistryPath.Length);
	memcpy(&RegUnicodeString.Buffer[SerRegistryPath.Length / 2], SerName.Buffer, SerName.Length);
	RegUnicodeString.MaximumLength = SerRegistryPath.MaximumLength + SerName.MaximumLength;
	RegUnicodeString.Length = SerRegistryPath.Length + SerName.Length;
	InitializeObjectAttributes(&objAttrs, &RegUnicodeString, OBJ_CASE_INSENSITIVE, NULL, NULL);
	status = ZwCreateKey(&hReg, KEY_ALL_ACCESS, &objAttrs, 0, NULL, REG_OPTION_VOLATILE, &Out);
	status = ZwSetValueKey(hReg, &ImagePathUn, NULL, REG_EXPAND_SZ, ODrvPath, 2 * wcslen(ODrvPath));
	status = ZwSetValueKey(hReg, &DisplayNameUn, NULL, REG_SZ, ServiceName, 2 * wcslen(ServiceName));
	status = ZwSetValueKey(hReg, &ErrorControlUn, NULL, REG_DWORD, &EC, 4);
	status = ZwSetValueKey(hReg, &StartUn, NULL, REG_DWORD, &Str, 4);
	status = ZwSetValueKey(hReg, &TypeUn, NULL, REG_DWORD, &Typ, 4);
	return hReg;
}

//ADrvPath ��������·����  ���磺L"\\??\\C:\\Users\\52pojie\\Desktop\\A.sys"
//ODrvPath ��������·����  ���磺L"\\??\\C:\\Users\\52pojie\\Desktop\\T.sys"
//ServiceName ���ܷ������� ���磺L"xixi"
BOOLEAN CamouflageDrvLoad(PWCHAR ADrvPath, PWCHAR ODrvPath, PWCHAR ServiceName) {
	NTSTATUS status = STATUS_SUCCESS;
	PDRIVER_OBJECT PTDrvObj = NULL;
	ULONG64 Out = 0;
	WCHAR ServiceNameBuffer[0x50] = { 0 };
	HANDLE HRegistry = NULL;

	//ӳ�䡢�޸�IAT
	int un = 0;
	PUCHAR PADriverSection = NULL;
	PLDR_DATA_TABLE_ENTRY PODriverSection = NULL;
	PLDR_DATA_TABLE_ENTRY NewPODriverSection = NULL;
	//����������ADrv����Ϣ
	PUCHAR Section = NULL;
	PUCHAR DllBase = NULL;
	ULONG32 DllSize = 0;

	UNICODE_STRING ADrvPathUn;
	UNICODE_STRING ODrvPathUn;
	//����������ODrv����Ϣ
	UNICODE_STRING OutU;
	UNICODE_STRING Out14[14];

	UNICODE_STRING AString;
	UNICODE_STRING OString;
	PKTHREAD thread = NULL;

	PUCHAR Head = NULL;

	PULONG64 ConfigAdd = 0;
	PULONG64 P_security_cookieAddress = NULL;

	struct _WDF_BIND_INFO* PWdfBindInfo = NULL;
	PULONG64 PWdfFunctions = NULL;
	PULONG64 PWdfDriverGlobals = NULL;

	HANDLE DrvH = NULL;
	OBJECT_ATTRIBUTES att = { 0 };
	UNICODE_STRING ObjectName = { 0 };

	PUNICODE_STRING PSTR = NULL;
	ULONG NtQueryObjReturnLen = 0;
	ShellContext DEContext = { 0 };
	WORK_QUEUE_ITEM WorkItem = { 0 };

	//��������ע���
	HRegistry = CreateRegistry(ODrvPath, ServiceName);
	DbgBreakPoint();
	try {
		//����
		ExAcquireResourceExclusiveLite(PIopDriverLoadResource, 1);
		//��������ǩ��ǿ��
		CIFun = *Pqword_14040EF40;
		DbgPrint("PSeValidateImageHeader here %p\n", Pqword_14040EF40);
		*Pqword_14040EF40 = MySeValidateImageHeader;

		//ӳ������
		RtlInitUnicodeString(&ADrvPathUn, ADrvPath);
		status = MiGenerateSystemImageNames(&ADrvPathUn, 0, 0, &OutU, Out14, &AString);
		//OUT : UN"��ǩ��.sys" 
		//Out14[0] : UN"Pathǰ׺" Out14[3] : UN"\Driver\"
		//String1 ͬ Path
		RtlInitUnicodeString(&ODrvPathUn, ODrvPath);
		status = MiGenerateSystemImageNames(&ODrvPathUn, 0, 0, &OutU, Out14, &OString);

		//DbgBreakPoint();
		//����DriverSection
		thread = MmAcquireLoadLock();
		status = MiObtainSectionForDriver(&AString, &ADrvPathUn, 0, 0, &PADriverSection);
		status = MiObtainSectionForDriver(&OString, &ODrvPathUn, 0, 0, &PODriverSection);
		MmReleaseLoadLock(thread);

		//ӳ��ADrv,��ӳ��ODrv
		Section = *(PULONG64)(PADriverSection + SectionOffset);//������
		DllBase = MiGetSystemAddressForImage(Section, 0, &un);

		status = MiMapSystemImage(Section, DllBase);
		
		//�ָ�����ǩ��ǿ��
		*Pqword_14040EF40 = CIFun;
		Head = RtlImageNtHeader(DllBase);
		DllSize = *(PULONG32)(Head + 0x50);

		//�ύDriverSection
		PODriverSection->SizeOfImage = DllSize;
		PODriverSection->DllBase = DllBase;
		status = MiConstructLoaderEntry(PODriverSection, &OutU, &OString, 0, 1, &NewPODriverSection);
		ExFreePoolWithTag(PODriverSection, 0);
		ExFreePoolWithTag(PADriverSection, 0);
		NewPODriverSection->Flags = 0x49104000;
		//flag 0x49104000


		//�޸�IAT
		if (!MakeIAT(DllBase)) {
			return FALSE;
		}


		//�޸�_security_cookie
		int size = 0;
		ConfigAdd = RtlImageDirectoryEntryToData(DllBase, 1, 0xA, &size);
		P_security_cookieAddress = ConfigAdd[0xb];
		SetWrite(P_security_cookieAddress, 1);
		*P_security_cookieAddress = 1;
		//SetWrite(P_security_cookieAddress, 0);

		//�޸�WDF
		if (IsWDF == TRUE) {
			PWdfBindInfo = ((ULONG64)P_security_cookieAddress) + 0x10;
			PWdfFunctions = PWdfBindInfo->FuncTable;
			//SetWrite(P_security_cookieAddress, 1);
			*PWdfFunctions = (ULONG64)ExAllocatePool(NonPagedPool, 0x1000);
			//SetWrite(P_security_cookieAddress, 0);
			if ((*PWdfFunctions) == NULL) {
				return FALSE;
			}
			memset(*PWdfFunctions, 0, 0x1000);
			PWdfDriverGlobals = ((ULONG64)PWdfFunctions) + 8;
			//SetWrite(P_security_cookieAddress, 1);
			*PWdfDriverGlobals = ExAllocatePool(NonPagedPool, 0x100);
			//SetWrite(P_security_cookieAddress, 0);
			if (*PWdfDriverGlobals == NULL) {
				ExFreePool(*PWdfFunctions);
				return FALSE;
			}memset(*PWdfDriverGlobals, 0, 0x100);
		}

		DbgPrint("DllBase:%p\n", DllBase);


		//����DriverObject������
		memcpy(ServiceNameBuffer, DrvObjNamePrefix, 2 * wcslen(DrvObjNamePrefix));
		memcpy(&ServiceNameBuffer[wcslen(DrvObjNamePrefix)], ServiceName, 2 * wcslen(ServiceName));
		RtlInitUnicodeString(&ObjectName, ServiceNameBuffer);
		att.Length = 0x30; att.Attributes = 0x250; att.ObjectName = &ObjectName;
		status = ObCreateObjectEx(0, *PIoDriverObjectType, &att, 0, &Out, 0x1A0, 0, 0, &PTDrvObj, 0);
		if (status != STATUS_SUCCESS) {
			return status;
		}
		memset(PTDrvObj, 0, 0x1a0);
		PTDrvObj->DriverExtension = &PTDrvObj[1];
		*(PULONG64)(&PTDrvObj[1]) = &PTDrvObj[0];
		for (int i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
			PTDrvObj->MajorFunction[i] = PIopInvalidDeviceRequest;
		}
		PTDrvObj->Type = 4; PTDrvObj->Size = 0x150;
		PTDrvObj->DriverInit = NewPODriverSection->EntryPoint;
		PTDrvObj->DriverSection = NewPODriverSection;
		PTDrvObj->DriverStart = DllBase;
		PTDrvObj->DriverSize = DllSize;
		PTDrvObj->Flags |= 2;
		//DbgBreakPoint();
		status = ObInsertObjectEx(PTDrvObj, 0, 1, 0, 0, 0, &DrvH);
		ExReleaseResourceLite(PIopDriverLoadResource);//����
		status = ObReferenceObjectByHandle(DrvH, 0, *PIoDriverObjectType, 0, &PTDrvObj, NULL);
		ZwClose(DrvH);
		PTDrvObj->HardwareDatabase = PCmRegistryMachineHardwareDescriptionSystemName;
		PTDrvObj->DriverName.Buffer = ExAllocatePool(NonPagedPool, ObjectName.MaximumLength);
		PTDrvObj->DriverName.Length = ObjectName.Length;
		PTDrvObj->DriverName.MaximumLength = ObjectName.MaximumLength;
		if (PTDrvObj->DriverName.Buffer == NULL) {
			return FALSE;
		}
		memcpy(PTDrvObj->DriverName.Buffer, ObjectName.Buffer, ObjectName.MaximumLength);

		//DriverEntry
		PSTR = ExAllocatePool(NonPagedPool, 0x1000);
		status = ZwQueryObject(HRegistry, 1, PSTR, 0x1000, &NtQueryObjReturnLen);
		DEContext.DrvObj = PTDrvObj;
		DEContext.PSTR = PSTR;
		KeInitializeEvent(&WaitWorkItem, SynchronizationEvent, FALSE);
		PULONG64 SetDriverEntry = &ShellDriverEntry;
		*SetDriverEntry = PTDrvObj->DriverInit;
		WorkItem.WorkerRoutine = Shim;
		WorkItem.Parameter = &DEContext;
		WorkItem.List.Flink = 0i64;
		ExQueueWorkItem(&WorkItem, DelayedWorkQueue);

		KeWaitForSingleObject(&WaitWorkItem, Executive, KernelMode, FALSE, NULL);
		//�ύ�豸
		IopReadyDeviceObjects(PTDrvObj);
		//�ͷ�
		ExFreePool(PSTR);
		ZwClose(HRegistry);
		if (IsWDF == TRUE) {
			ExFreePool(*PWdfFunctions);
			ExFreePool(*PWdfDriverGlobals);
			*PWdfFunctions = 0;
			*PWdfDriverGlobals = 0;
		}
		return TRUE;
	}except(1) {
		return FALSE;
	}
}
